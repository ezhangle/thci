/*
 *
 *    Copyright (c) 2016-2018 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements functions that update the 6LoWPAN radio. These
 *    functions are called from the auto-updater (AUPD), which is single-
 *    threaded and does not instantiate some modules, including nl-er-logger.
 *    Hence, we use printfs instead of NL_LOG and NL_CPU_spin instead of
 *    nl_task_sleep.
 *
 */


#include <thci_config.h>

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

#include "thci_update.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <nlerlog.h>

#include <nlboardconfig.h>
#include <nlconsole.h>
#include <nlconsole_utils.h>
#include <nlproduct.h>
#include <nlxmodem.h>
#include <nlmacros.h>
#include <nlassert.h>
#include <nlwatchdog.h>

#include <thci.h>
#include <thci_module_ncp_uart.h>
#include <openthread/spinel.h>

/**
 * DEFINES
 */

#define UPDATE_ATTEMPTS                     (3)
#define UPDATE_VERSION_REQUEST_ATTEMPTS     (2)
#define UPDATE_START_OF_TRANSFER_ATTEMPTS   (2)
#define UPDATE_BUFFER_SIZE                  (128) // must match xmodem payload size.
#define UPDATE_VERSION_DELAY_MS             (5)
#define UPDATE_VERSION_DEADLINE_MS          (500)
#define UPDATE_RESPONSE_DELAY_MS            (1)
#define UPDATE_RESPONSE_DEADLINE_MS         (1000)
#define UPDATE_SEND_DELAY_MS                (1)
#define UPDATE_SEND_DEADLINE_MS             (5)
#define UPDATE_UART_BAUD_RATE               (115200)
#define UPDATE_UART_FLOW_CONTROL            (false)
#define UPDATE_PROMPT_DELAY_MS              (1)
#define UPDATE_PROMPT_DEADLINE_MS           (2000)
#define UPDATE_RESET_HOLD_TIME_MS           (3)
#define UPDATE_RESET_BOOT_DELAY_TIME_MS     (1000)

// VERSION_STRING_SIZE must be larger than the largest expected version string.
#define VERSION_STRING_SIZE             (96)
#define VERSION_STRING_TERMINATOR       '\n'

/**
 * PROTOTYPES
 */

extern otError InitializeInternal(bool inMandatoryNcpReset, bool inAPIInitialize, thci_callbacks_t *aCallbacks,
                                  thciUartDataFrameCallback_t aDataCB, thciUartControlFrameCallback_t aControlCB);
extern otError FinalizeInternal(bool inAPIFinalize);

/**
 * GLOBALS
 */

const uint8_t kDontCareTransactionId;

/**
 * MODULE IMPLEMENTATION
 */

static void DelayMs(nl_time_ms_t delay)
{
    NL_CPU_spinWaitUs(1000 * delay);
}

static void PetWatchdog(void)
{
    //Prevents a possible reset when aupd is running.
    nl_wdt_refresh();
}

static void AssertNcpResetGpio(bool aAssert)
{
    int resetGpioNum = GPIO_NUM_FROM_ID(kEm357Reset);

    // output, active low
    const uint8_t value = aAssert ? 0 : 1;

    nl_gpio_set_output(resetGpioNum, value);
}

static void DeassertNcpBootloaderGpio(void)
{
    int bootloaderGpioNum = GPIO_NUM_FROM_ID(kEm357Bootload);
    int mode = 0; // disable mode = 0

    // disable GPIO mode
    nl_gpio_setmode(bootloaderGpioNum, mode);
}

static void AssertNcpBootloaderGpio(bool aAssert)
{
    int bootloaderGpioNum = GPIO_NUM_FROM_ID(kEm357Bootload);
    int high = 1;
    int low = 0;
    int mode = 1; // gpio mode == 1

    nl_gpio_setmode(bootloaderGpioNum, mode);
    nl_gpio_set_output(bootloaderGpioNum, aAssert ? low : high);
}

static int EnableConsole(const nl_console_t *aConsole, uint32_t aFlags)
{
    const nl_console_config_t preConfig    = {
        UPDATE_UART_BAUD_RATE,
        aFlags
    };

    int retval = nl_console_enable(aConsole, true, &preConfig);

    if (retval)
    {
        printf("%s: Failed to set console flags, err = %d\n", __FUNCTION__, retval);
    }

    return retval;
}

static int DisableConsole(const nl_console_t *aConsole)
{
    return nl_console_enable(aConsole, false, NULL);
}

static int QueryNcpVersionString(char *aVersion, size_t aLength)
{
    int retval = 0;
    otError error;
    const bool apiInitialize = true;
    const bool mandatoryReset = true;

    error = InitializeInternal(mandatoryReset, !apiInitialize, NULL, NULL, NULL);
    nlREQUIRE_ACTION(error == OT_ERROR_NONE, done, retval = -ENXIO);

    // Query the version string from the NCP
    error = thciGetVersionString(aVersion, aLength);
    nlREQUIRE_ACTION(error == OT_ERROR_NONE, done, retval = -EIO);

 done:
    FinalizeInternal(!apiInitialize);

    return retval;
}

static int QueryFileVersionString(nlfs_file_t *aFile, char *aVersionString, size_t aLength)
{
    int retval;
    int fsval;
    size_t bytesRead;
    size_t i;

    nlREQUIRE_ACTION(aVersionString && aLength, done, retval = -EINVAL);

    // find the version string at the beginning of the file.
    fsval = nlfs_seek(aFile, 0, BEGINNING);
    nlREQUIRE_ACTION(fsval == 0, done, retval = -EIO);

    bytesRead = nlfs_read(aFile, aVersionString, aLength);
    nlREQUIRE_ACTION(bytesRead == aLength, done, retval = -EIO);

    // find the version string terminator in the string and seek to that position in the file.
    for (i = 0 ; i < bytesRead ; i++)
    {
        if (aVersionString[i] == VERSION_STRING_TERMINATOR)
        {
            aVersionString[i] = '\0';
            break;
        }
    }

    nlREQUIRE_ACTION(i < bytesRead, done, retval = -EIO);
    
    retval = 0; // success.

 done:
    return retval;
}

static int WaitForReady(const nl_console_t *aConsole)
{
    int retval = 0;

    size_t attempts = UPDATE_SEND_DEADLINE_MS / UPDATE_SEND_DELAY_MS;

    while (1)
    {
        if (nl_console_canput(aConsole))
        {
            break;
        }

        if (attempts-- == 0)
        {
            retval = -ETIMEDOUT;
            break;
        }

        DelayMs(UPDATE_SEND_DELAY_MS);
    }

    return retval;
}

static int WriteModem(const nl_console_t *aConsole, const uint8_t *aBuffer, size_t aLength)
{
    int retval = 0;

    while (aLength--)
    {
        retval = WaitForReady(aConsole);
        nlREQUIRE(retval == 0, done);

        nl_console_putchar(aConsole, *aBuffer);

        aBuffer++;
    }

 done:
    return retval;
}

static int WaitForPrompt(const nl_console_t *aConsole)
{
    int retval;
    const uint8_t kNewlines[] = {
        '\n'
    };

    retval = WriteModem(aConsole, kNewlines, sizeof(kNewlines));
    // With the existing bootloader there is no prompt to wait for, 
    // so delaying 100msec serves to give the bootloader an opportunity
    // to fully bootup. This has improved host - ncp interaction.
    DelayMs(100);

    return retval;
}

static int ExitBootloader(const nl_console_t *aConsole)
{
    int retval;
    const bool startBootloader = true;

    thciHardResetNcp(!startBootloader);

    retval = DisableConsole(aConsole);//, NL_CONSOLE_FLOWCONTROL_ENABLE);

    return retval;
}

static int EnterBootloader(const nl_console_t *aConsole)
{
    int retval;
    const bool startBootloader = true;

    thciHardResetNcp(startBootloader);

    EnableConsole(aConsole, UPDATE_UART_FLOW_CONTROL);

    retval = WaitForPrompt(aConsole);

    return retval;
}

static int InitiateUpload(const nl_console_t *aConsole)
{
    int retval = -EAGAIN;
    const uint8_t kUploadCommand[] = {
        'x'
    };
    const uint8_t kResponse = 'C';
    uint32_t attempts = UPDATE_START_OF_TRANSFER_ATTEMPTS;

    while (retval != 0)
    {
        if (attempts-- == 0)
        {
            break;
        }

        nl_console_flush(aConsole, NL_CONSOLE_BOTH_DIR);
        WriteModem(aConsole, kUploadCommand, sizeof(kUploadCommand));

        retval = nl_console_utils_findchar_cb(aConsole, kResponse, UPDATE_RESPONSE_DEADLINE_MS, UPDATE_RESPONSE_DELAY_MS, PetWatchdog);
    }

    return retval;
}

static int UpdateNcpWithFile(nlfs_file_t *aFile)
{
    int retval = 0;
    uint8_t buff[UPDATE_BUFFER_SIZE];
    const nl_console_t *console = NL_PRODUCT_CONSOLE(6LOWPAN);
    uint8_t debug = 0;

    retval = EnterBootloader(console);
    nlREQUIRE_ACTION(retval == 0, done, debug = 1);

    retval = InitiateUpload(console);
    nlREQUIRE_ACTION(retval == 0, done, debug = 2);

    retval = nl_xmodem_send(console, aFile, buff, sizeof(buff));

 done:
    if (retval)
    {
        printf("Send failed debug=%d, retval=%d\n", debug, retval);
    }

    ExitBootloader(console);

    return retval;
}

static int QueryBootloaderVersion(const nl_console_t *aConsole, char *aBuffer, size_t aLength)
{
    int retval = 0;

    const uint8_t kRequest[] = {
        'v'
    };

    const uint8_t kNewline = '\n';

    char buffer[UPDATE_BUFFER_SIZE];

    nl_console_flush(aConsole, NL_CONSOLE_BOTH_DIR);
    retval = WriteModem(aConsole, kRequest, sizeof(kRequest));

    if (retval == 0)
    {
        size_t remaining = sizeof(buffer) - 1;
        char *p = buffer;

        while (1)
        {
            if (remaining == 0)
            {
                retval = -EINVAL;
                break;
            }

            retval = nl_console_utils_waitchar_cb(aConsole, p, UPDATE_VERSION_DEADLINE_MS, UPDATE_VERSION_DELAY_MS, PetWatchdog);

            if (retval != 0)
            {
                // legacy support: check if we at least got something, if we did, then take what we have.
                if (p != buffer)
                {
                    // null terminate the string
                    *p++ = 0;
                    retval = 0;
                }
                break;
            }

            if (*p == kNewline)
            {
                // null terminate the string and overwrite the newline
                *p = 0;
                break;
            }

            p++;
            remaining--;
        }
    }

    if (retval == 0)
    {
        strncpy(aBuffer, buffer, aLength);
    }

    return retval;
}

int thciGetBootloaderVersion(char *aBuffer, size_t aLength)
{
    int retval = 0;

    const nl_console_t *console = NL_PRODUCT_CONSOLE(6LOWPAN);

    DisableConsole(console);

    retval = EnterBootloader(console);

    if (retval == 0)
    {
        const uint8_t kCommand[] = {
            '\n'
        };

        retval = WriteModem(console, kCommand, sizeof(kCommand));
    }

    if (retval == 0)
    {
        uint8_t attempts = UPDATE_VERSION_REQUEST_ATTEMPTS;

        while (attempts-- != 0) {
            retval = QueryBootloaderVersion(console, aBuffer, aLength);

            if (retval == 0){
                break;
            }
        }
    }

    retval = ExitBootloader(console);

    return retval;
}

int thciGetNCPVersionTest(char *aBuffer, size_t aLength)
{
    int retval;

    retval = QueryNcpVersionString(aBuffer, aLength);

    return retval;
}

int thciFirmwareUpdate(nlfs_image_location_t aImageLoc)
{
    int retval;
    nlfs_file_t file;
    unsigned attempts = UPDATE_ATTEMPTS;
    char ncpVersionString[VERSION_STRING_SIZE];
    char fileVersionString[VERSION_STRING_SIZE];
    bool versionMatch = false;

    memset(&file, 0, sizeof(nlfs_file_t));

    printf("%s: Starting update...\n", __FUNCTION__);

    // open the file
    retval = nlfs_open(kEm357Fw, READ_ONLY, aImageLoc, &file);
    nlREQUIRE_ACTION(retval == 0, done, printf("%s: Failed to open source file\n", __FUNCTION__));

    printf("%s: Checking versions...\n", __FUNCTION__);

    retval = QueryFileVersionString(&file, fileVersionString, VERSION_STRING_SIZE);
    nlREQUIRE_ACTION(retval == 0, done, printf("Failed to find version string in image file.\n"));

    retval = QueryNcpVersionString(ncpVersionString, VERSION_STRING_SIZE);

    if (retval == 0)  // if this fails it could just mean there is no app image on the NCP.
    {
        // compare the versions and if they match, skip the update.
        if(0 == strcmp(fileVersionString, ncpVersionString))
        {
            versionMatch = true;
            goto done;
        }
        else
        {
            printf("%s: Versions compared but don't match.\n", __FUNCTION__);
        }
    }

    do
    {   
        // Seek past the VERSION_STRING_TERMINATOR for the subsequent file update operation. 
        nlfs_seek(&file, strlen(fileVersionString) + 1, BEGINNING);

        // the file should have already seeked past the version string.
        retval = UpdateNcpWithFile(&file);

        if (retval == 0)
        {
            break;
        }

    } while (--attempts);

    printf("%s: End of update, result = %d.\n", __FUNCTION__, retval);

    nlREQUIRE(retval == 0, done);
    
    retval = QueryNcpVersionString(ncpVersionString, VERSION_STRING_SIZE);
    nlREQUIRE(retval == 0, done);
    
    printf("%s: Version read from NCP: %s\n", __FUNCTION__, ncpVersionString);

 done:
    if (nlfs_is_open(&file))
    {
        nlfs_close(&file);
    }

    if (retval != 0)
    {
        printf("%s: Update failed, result = %d\n", __FUNCTION__, retval);
    }
    else if (versionMatch)
    {
        printf("%s: No update needed - same versions.\n", __FUNCTION__);
    }

    return retval;
}

void thciHardResetNcp(bool aStartBootloader)
{
    AssertNcpBootloaderGpio(aStartBootloader);

    DelayMs(1);

    // reset chip
    AssertNcpResetGpio(true);
    DelayMs(UPDATE_RESET_HOLD_TIME_MS);
    AssertNcpResetGpio(false);

    // wait for em35x to boot up before exiting as the
    // bootloader must have time to read the bootloader
    // gpio.
    DelayMs(UPDATE_RESET_BOOT_DELAY_TIME_MS);

    DeassertNcpBootloaderGpio();
}

#endif //THCI_CONFIG_USE_OPENTHREAD_ON_NCP
