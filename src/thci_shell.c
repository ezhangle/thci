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
 *      This file defines the shell interface to various wpan commands.
 *
 */

#include <errno.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include <nlertime.h>
#include <nlerlog.h>
#include <nlutilities.h>
#include <nlassert.h>

#include <thci_config.h>
#include <thci_shell.h>
#include <thci_update.h>
#include <thci_safe_api.h>
#include <thci_cert.h>

#include <lwip/ip6_addr.h>

// Preprocessor Definitions

#define DECLARE_LONG_OPTION(long_opt, short_opt, flag) { (char *)long_opt, flag, NULL, short_opt }
#define DECLARE_NO_OPTION(opt)                         DECLARE_LONG_OPTION(kOptLong##opt, kOptShort##opt, no_argument)
#define DECLARE_OPTIONAL_OPTION(opt)                   DECLARE_LONG_OPTION(kOptLong##opt, kOptShort##opt, optional_argument)
#define DECLARE_REQUIRED_OPTION(opt)                   DECLARE_LONG_OPTION(kOptLong##opt, kOptShort##opt, required_argument)
#define DECLARE_TERMINATING_OPTION()                   DECLARE_LONG_OPTION(0, 0, 0)

// Long Option Values

enum
{
    kOptShortAdd                       = 1010,
    kOptShortRemove                    = 1011,
    kOptShortPriority                  = 1012,
    kOptShortIPv6Address               = 1013,
    kOptShortIPv6PrefixLength          = 1016
};

static const char * const kOptLongAdd               = "add";
static const char * const kOptLongRemove            = "remove";
static const char * const kOptLongPriority          = "priority";
static const char * const kOptLongIPv6Address       = "ipv6-address";
static const char * const kOptLongIPv6PrefixLength  = "ipv6-prefix-length";


enum 
{
    kHaveNone =         0x00,
    kHaveAddress =      0x01,
    kHavePrefixLength = 0x02,
    kHaveAction =       0x04,
    kHavePriority =     0x08,
    kHaveAll =          kHaveAddress | kHavePrefixLength | kHaveAction | kHavePriority
};

typedef int (*handler_func)(int argc, const char *argv[]);
typedef void (*helper_func)(void);

struct command_entry_t {
    handler_func handler;
    helper_func helper;
    const char *name;
    const char *args;
    const char *description;
};

static int handle_help(int argc, const const char *argv[]);
static int display_cmd_help(const struct command_entry_t *cmd_entry);



static void display_cmd_description(const char *prepend,
                                    const struct command_entry_t *cmd_entry)
{
    char name_args[64];
    sprintf(name_args, "%s%s %s", prepend, cmd_entry->name, cmd_entry->args);
    printf("  %-32s %s\n", name_args, cmd_entry->description);
}

static int display_cmd_help(const struct command_entry_t *cmd_entry)
{
    printf("\n");
    display_cmd_description("", cmd_entry);

    if (cmd_entry->helper != NULL)
    {
        printf("==========\n");
        cmd_entry->helper();
    }
    return 0;
}

static void LogError(const char *inName, uint32_t inCode)
{
    NL_LOG_CRIT(lrAPP, "Function %s  failed with error = %u\n", inName, inCode);
}

#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP

static int handle_update(int argc, const char *argv[])
{
    int retval = 0;
    nlfs_image_location_t loc = INSTALLED;

    (void)argc;
    (void)argv;

    if (retval == 0)
    {
        retval = thciFirmwareUpdate(loc);
    }

    if (retval == -ENOENT)
    {
        NL_LOG_CRIT(lrAPP, "file not found, trying alternate location\n");

        loc = ALTERNATE;
        retval = thciFirmwareUpdate(loc);
    }

    if (retval == 0)
    {
        NL_LOG_CRIT(lrAPP, "update successful\n");
    }
    else
    {
        LogError(__FUNCTION__, retval);
    }

    return retval;
}

static int handle_bootloader_version(int argc, const char *argv[])
{
    int retval;
    char version[256];

    (void)argc;
    (void)argv;

    // NOTE: thciGetBootloaderVersion will reset the NCP and configure it
    //       to reboot the bootloader only.  After acquiring the version 
    //       from the bootloader it will reset the NCP a second time
    //       allowing it to reboot the application, if any.
    retval = thciGetBootloaderVersion(version, sizeof(version));
    nlREQUIRE(!retval, done);

    NL_LOG_CRIT(lrAPP, "Bootloader version = '%s'\n", version);

 done:
    if (retval)
    {
        LogError(__FUNCTION__, retval);
    }

    return retval;
}

static int handle_version_test(int argc, const char *argv[])
{
    int retval;
    char version[256];

    (void)argc;
    (void)argv;

    retval = thciGetNCPVersionTest(version, sizeof(version));
    nlREQUIRE(!retval, done);

    NL_LOG_CRIT(lrAPP, "ncp version = '%s'\n", version);

 done:
    if (retval)
    {
        LogError(__FUNCTION__, retval);
    }

    return retval;
}

static int handle_ncp_reset(int argc, const char *argv[])
{
    otError error;

    (void)argc;
    (void)argv;

    // NOTE: This command will cause the NCP to reset and thus cause it to 
    // get out of sync with NetworkManager.
    error = thciSafeHardResetNCP();
    nlREQUIRE(error == OT_ERROR_NONE, done);
    
    NL_LOG_CRIT(lrAPP, "thciSafeHardResetNCP succeeded.\n");

 done:
    if (error != OT_ERROR_NONE)
    {
        LogError(__FUNCTION__, (uint32_t)error);
    }

    return 0;
}

#endif /* THCI_CONFIG_USE_OPENTHREAD_ON_NCP */

static int handle_mac_params(int argc, const char *argv[])
{
    otMacCounters counters;
    otError status;
    int retval = 0;

    const struct {
        const char *mName;
        uint32_t *mValue;
    } displayPairs[] = {
        {"mTxTotal              ", &counters.mTxTotal},
        {"mTxUnicast            ", &counters.mTxUnicast},
        {"mTxBroadcast          ", &counters.mTxBroadcast},
        {"mTxAckRequested       ", &counters.mTxAckRequested},
        {"mTxAcked              ", &counters.mTxAcked},
        {"mTxNoAckRequested     ", &counters.mTxNoAckRequested},
        {"mTxData               ", &counters.mTxData},
        {"mTxDataPoll           ", &counters.mTxDataPoll},
        {"mTxBeacon             ", &counters.mTxBeacon},
        {"mTxBeaconRequest      ", &counters.mTxBeaconRequest},
        {"mTxOther              ", &counters.mTxOther},
        {"mTxRetry              ", &counters.mTxRetry},
        {"mTxErrCca             ", &counters.mTxErrCca},
        {"mTxErrAbort           ", &counters.mTxErrAbort},
        {"mRxTotal              ", &counters.mRxTotal},
        {"mRxUnicast            ", &counters.mRxUnicast},
        {"mRxBroadcast          ", &counters.mRxBroadcast},
        {"mRxData               ", &counters.mRxData},
        {"mRxDataPoll           ", &counters.mRxDataPoll},
        {"mRxBeacon             ", &counters.mRxBeacon},
        {"mRxBeaconRequest      ", &counters.mRxBeaconRequest},
        {"mRxOther              ", &counters.mRxOther},
        {"mRxAddressFiltered    ", &counters.mRxAddressFiltered},
        {"mRxDestAddrFiltered   ", &counters.mRxDestAddrFiltered},
        {"mRxDuplicated         ", &counters.mRxDuplicated},
        {"mRxErrNoFrame         ", &counters.mRxErrNoFrame},
        {"mRxErrUnknownNeighbor ", &counters.mRxErrUnknownNeighbor},
        {"mRxErrInvalidSrcAddr  ", &counters.mRxErrInvalidSrcAddr},
        {"mRxErrSec             ", &counters.mRxErrSec},
        {"mRxErrFcs             ", &counters.mRxErrFcs},
        {"mRxErrOther           ", &counters.mRxErrOther},
        {NULL, NULL}
    };

    (void)argc;
    (void)argv;

    status = thciSafeGetMacCounters(&counters);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

    for (size_t i = 0 ; displayPairs[i].mName != NULL && displayPairs[i].mValue != NULL ; i++)
    {
        NL_LOG_CRIT(lrAPP, "%s= %u\n", displayPairs[i].mName, *displayPairs[i].mValue);
    }

 done:

    if (retval)
    {
        LogError(__FUNCTION__, status);
    }

    return retval;
}

static int handle_diags_cmd(int argc, const char *argv[])
{
    otError status;
    int retval = 0;
    char str[128];
    size_t index = 0;
    int len;
    const char *prefix = "diag";
    int i;

    argc--;
    argv++;

    len = strlen(prefix);

    memcpy(&str[index], prefix, len);
    index += len;
    str[index++] = ' ';

    for (i = 0 ; i < argc ; i++)
    {
        len = strlen(argv[i]);
        memcpy(&str[index], argv[i], len);

        index += len;
        str[index++] = ' ';
    }

    // replace the last ' ' with a NULL term.
    str[index] = '\0';

    NL_LOG_CRIT(lrAPP, "handle_diags_cmd: string = %s\n", str);

    status = thciSafeMfgDiagsCmd(str);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

 done:
    return retval;
}

static int handle_version(int argc, const char *argv[])
{
    char version[128];
    int retval = 0;
    otError error = thciSafeGetVersionString(&version[0], sizeof(version));

    (void)argc;
    (void)argv;

    nlREQUIRE_ACTION(error == OT_ERROR_NONE, done, retval = -EIO);

    NL_LOG_CRIT(lrAPP, "OT version = %s\n", version);

 done:
    return retval;
}

static int handle_ext_route(int argc, const char *argv[])
{
    otExternalRouteConfig config;
    otError result;
    bool add;
    otRoutePreference priority;
    uint8_t flags = kHaveNone;
    ip6_addr_t address;
    uint8_t prefixLen;
    unsigned int errors = 0;
    int c = 0;
    const char *const short_options = "";
    const struct option long_options[] = {
        DECLARE_NO_OPTION(Add),
        DECLARE_NO_OPTION(Remove),
        DECLARE_REQUIRED_OPTION(Priority),
        DECLARE_REQUIRED_OPTION(IPv6Address),
        DECLARE_REQUIRED_OPTION(IPv6PrefixLength),
        DECLARE_TERMINATING_OPTION()
    };
    // command format
    // ext_route --<add | remove> --ipv6-address  XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX --ipv6-prefix-length 48 --priority <low|medium|high>

    while (!errors && (c = getopt_long(argc, argv, short_options, long_options, NULL)) != -1)
    {
        switch (c)
        {
        case kOptShortAdd:
            add = true;
            flags |= kHaveAction;
            break;

        case kOptShortRemove:
            add = false;
            flags |= kHaveAction;
            break;

        case kOptShortPriority:
            if (strcmp(optarg, "low") == 0)
            {
                priority = OT_ROUTE_PREFERENCE_LOW;
                flags |= kHavePriority;
            }
            else if (strcmp(optarg, "medium") == 0 || 
                     strcmp(optarg, "med") == 0)
            {
                priority = OT_ROUTE_PREFERENCE_MED;
                flags |= kHavePriority;
            }
            else if (strcmp(optarg, "high") == 0)
            {
                priority = OT_ROUTE_PREFERENCE_HIGH;
                flags |= kHavePriority;
            }
            else
            {
                errors++;
                NL_LOG_CRIT(lrAPP, "Error: priority \"%s\".\n", optarg);
            }
            break;

        case kOptShortIPv6Address:
            
            if (!ip6addr_aton(optarg, &address))
            {
                errors++;
            }

            if (errors)
            {
                NL_LOG_CRIT(lrAPP, "Error: Invalid IPv6 address \"%s\".\n", optarg);
            } 
            else
            {
                flags |= kHaveAddress;
            }

            break;

        case kOptShortIPv6PrefixLength:
        {
            char *result;

            prefixLen = strtoul(optarg, &result, 10);

            errors += ((result == optarg) || (result == NULL));

            if (errors)
            {
                NL_LOG_CRIT(lrAPP, "Error: Invalid prefix length \"%s\".\n", optarg);
            }
            else
            {
                flags |= kHavePrefixLength;
            }

            break;
        }

        default:
            NL_LOG_CRIT(lrAPP, "Error: Unknown option '%c'!\n", optopt);
            errors++;
            break;
        }
    }

    argc -= optind;
    argv += optind;

    // Be sure to reset optind, so that back-to-back command
    // invocations work correctly.

    optind = 0;

    if (flags != kHaveAll)
    {
        if (!(flags & kHavePrefixLength))
        {
            NL_LOG_CRIT(lrAPP, "Error: A prefix length; --%s must be supplied.\n", kOptLongIPv6PrefixLength);
        }

        if (!(flags & kHaveAddress))
        {
            NL_LOG_CRIT(lrAPP, "Error: An address; --%s must be supplied. <XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX>\n", kOptLongIPv6Address);
        }

        if (!(flags & kHaveAction))
        {
            NL_LOG_CRIT(lrAPP, "Error: An action; --%s or --%s must be supplied.\n", kOptLongAdd, kOptLongRemove);
        }

        if (!(flags & kHavePriority))
        {
            NL_LOG_CRIT(lrAPP, "Error: A route priority; --%s must be supplied. <low | medium | high>\n", kOptLongPriority);
        }

        errors++;
    }

    if (!errors)
    {
        otError threadError;

        config.mStable = true;
        config.mPrefix.mLength = prefixLen;
        config.mPreference = priority;
        memcpy(config.mPrefix.mPrefix.mFields.m8, address.addr, sizeof(address.addr));

        if (add)
        {
            threadError = thciSafeAddExternalRoute(&config);
        }
        else
        {
            threadError = thciSafeRemoveExternalRoute(&config.mPrefix);
        }

        if (threadError != OT_ERROR_NONE)
        {
            NL_LOG_CRIT(lrAPP, "Error: Thci operation failed %d\n", threadError);
        }
        else
        {
            NL_LOG_CRIT(lrAPP, "Successfully %s route\n", ((add) ? "added" : "removed"));
        }
    }

 done:
    return 0;
}

#ifdef BUILD_FEATURE_THCI_CERT
static void handle_corrupt_help(void)
{
    NL_LOG_CRIT(lrAPP, "\n"
                "Corrupt - Utility to flip <num> random bits in rx/tx frames \n"
                "          for cert testing.                                 \n"
                "corrupt <enable/disable> <rx/tx/all> <num>                  \n"
                "If no direction is given, then all is assumed.              \n"
                "If setting to disabled, then num is ignored.                \n");
}
#endif // BUILD_FEATURE_THCI_CERT

#ifdef BUILD_FEATURE_THCI_CERT
static int handle_corrupt(int argc, const char *argv[])
{
    enum direction_t
    {
        DIR_TX = 0x01,
        DIR_RX = 0x02,
        DIR_ALL = 0x03
    };

    bool enable;
    enum direction_t dir;
    int bit_count = 0;

    int retval = 0;

    // First entry is command name
    argc--;
    argv++;

    nlREQUIRE_ACTION(argc > 0, done,
                     NL_LOG_CRIT(lrAPP, "ERROR: Not enough arguments.\n"); retval = -EINVAL);

    //Get the state that we are setting

    if (!strcmp(argv[0], "enable"))
    {
        enable = true;
    }
    else if (!strcmp(argv[0], "disable"))
    {
        enable = false;
    }
    else
    {
        NL_LOG_CRIT(lrAPP, "ERROR: Invalid state. Valid options are enable, disable.\n");
        nlREQUIRE_ACTION(false, done, retval = -EINVAL);
        //TODO: is there some sort of exit function that would be better here?
    }

    argv++;
    argc--;

    //Enable requires more arguments
    nlREQUIRE_ACTION(argc > 0 || !enable,
                     done,
                     NL_LOG_CRIT(lrAPP, "ERROR: Not enough arguments.\n"); retval = -EINVAL);

    // Get the direction
    if (argc > 1 || (!enable && argc == 1))
    {
        //There is a direction and a num
        if (!strcmp(argv[0], "rx"))
        {
            dir = DIR_RX;
        }
        else if (!strcmp(argv[0], "tx"))
        {
            dir = DIR_TX;
        }
        else if (!strcmp(argv[0], "all"))
        {
            dir = DIR_ALL;
        }
        else
        {
            NL_LOG_CRIT(lrAPP, "ERROR: Invalid direction. Valid options are rx, tx, all\n");
            nlREQUIRE_ACTION(false, done, retval = -EINVAL);
        }
        argv++;
        argc--;
    }
    else
    {
        //If no direction, default to all
        dir = DIR_ALL;
    }

    // Get the bit count
    if (argc == 1)
    {
        bit_count = strtol(argv[0], NULL, 0);

        nlREQUIRE_ACTION(bit_count > 0,
                         done,
                         NL_LOG_CRIT(lrAPP, "ERROR: Invalid input for number of bits.\n"); retval = -EINVAL);

        nlREQUIRE_ACTION(bit_count <= 3,
                         done,
                         NL_LOG_CRIT(lrAPP, "ERROR: Bit count should be between 1 and 3.\n"); retval = -EINVAL);
    }
    else if (argc > 1)
    {
        NL_LOG_CRIT(lrAPP, "ERROR: Too many arguments.\n");
        nlREQUIRE_ACTION(false, done, retval = -EINVAL);
    }

    // Now actually do the setting
    if (!enable)
    {
        // Disable by setting bit count to 0
        bit_count = 0;
    }

    if (dir & DIR_RX)
    {
        thci_cert_set_rx_corrupt_bits(bit_count);
    }

    if (dir & DIR_TX)
    {
        thci_cert_set_tx_corrupt_bits(bit_count);
    }

done:
    if (retval == -EIO)
    {
        handle_corrupt_help();
    }
    return retval;
}
#endif // BUILD_FEATURE_THCI_CERT

static void handle_cmd(const struct command_entry_t *cmd_set, int argc, const char *argv[])
{
    int i;

    nlREQUIRE_ACTION(argc, done, NL_LOG_CRIT(lrAPP, "Missing cmd\n"));

    for (i = 0; cmd_set[i].name != NULL; i++)
    {
        if (strcmp(argv[0], cmd_set[i].name) == 0)
        {
            if (argc > 1 && strcmp(argv[1], "help") == 0)
            {
                display_cmd_help(&cmd_set[i]);
            }
            else
            {
                int status = cmd_set[i].handler(argc, argv);
                if (status == -EINVAL)
                    display_cmd_help(&cmd_set[i]);
            }

            goto done;
        }
    }

    NL_LOG_CRIT(lrAPP, "Unknown cmd '%s'\n", argv[0]);
    handle_help(0, NULL);

done:
    return;
}

static const struct command_entry_t cmd_list[] = {
    { handle_help, NULL, "help", "",
        "Display list of commands" },
#if THCI_CONFIG_USE_OPENTHREAD_ON_NCP
    { handle_bootloader_version, NULL, "bootloader_version", "",
        "Display the NCP bootloader version" },
    { handle_version_test, NULL, "version_test", "",
        "Try to get the NCP version without starting NM." },
    { handle_update, NULL, "update", "",
        "Update the NCP firmware" },
    { handle_ncp_reset, NULL, "ncp_reset", "",
        "Perform a hard reset on the NCP." },
#endif
    { handle_mac_params, NULL, "mac_counters", "",
        "Query and display MAC counters." },
    { handle_diags_cmd, NULL, "diag", "",
        "Pass various diagnostic command strings to Openthread." },
    { handle_version, NULL, "version", "",
        "Display the OpenThread version string." },
    { handle_ext_route, NULL, "ext_route", "",
        "Add/Remove an external route to OpenThread."},
#ifdef BUILD_FEATURE_THCI_CERT
    { handle_corrupt, handle_corrupt_help, "corrupt", "<enable/disable> <rx/tx/all> <num>",
        "Toggle random bits on rx/tx." },
#endif // BUILD_FEATURE_THCI_CERT
    { NULL, NULL, NULL, NULL, NULL }
};

static int handle_help(int argc, const char *argv[])
{
    int i;

    printf("COMMANDS:\n");

    for (i = 0; cmd_list[i].name != NULL; i++)
    {
        display_cmd_description("", &cmd_list[i]);
    }

    return 0;
}

int thciShellHandleCommand(int argc, const char * argv[])
{
    handle_cmd(cmd_list, argc - 1, argv + 1);

    return 0;
}

int thciShellMfgStart(void)
{
    char str[] = "diag start";
    otError status;
    int retval = 0;

    status = thciSafeMfgDiagsCmd(str);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

 done:
    return retval;
}

int thciShellMfgSetChannel(uint16_t inChannel)
{
    char str[64];
    otError status;
    int retval = 0;
    int n;

    n = snprintf(str, sizeof(str), "diag channel %u", inChannel);
    nlREQUIRE_ACTION(n >= 0 && n < sizeof(str), done, retval = -EINVAL);

    str[n] = '\0';

    status = thciSafeMfgDiagsCmd(str);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

 done:
    return retval;
}

int thciShellMfgSetPower(int inPower)
{
    char str[64];
    otError status;
    int retval = 0;
    int n;

    n = snprintf(str, sizeof(str), "diag power %d", inPower);
    nlREQUIRE_ACTION(n >= 0 && n < sizeof(str), done, retval = -EINVAL);

    str[n] = '\0';

    status = thciSafeMfgDiagsCmd(str);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

 done:
    return retval;
}

int thciShellMfgSetGpio(uint16_t inPin, uint8_t inValue)
{
    char str[64];
    otError status;
    int retval = 0;
    int n;

    n = snprintf(str, sizeof(str), "diag gpio set %u %u", inPin, inValue);
    nlREQUIRE_ACTION(n >= 0 && n < sizeof(str), done, retval = -EINVAL);

    str[n] = '\0';

    status = thciSafeMfgDiagsCmd(str);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

 done:
    return retval;
}

int thciShellMfgGetGpio(uint16_t inPin)
{
    char str[64];
    otError status;
    int retval = 0;
    int n;

    n = snprintf(str, sizeof(str), "diag gpio get %u", inPin);
    nlREQUIRE_ACTION(n >= 0 && n < sizeof(str), done, retval = -EINVAL);

    str[n] = '\0';

    status = thciSafeMfgDiagsCmd(str);
    nlREQUIRE_ACTION(status == OT_ERROR_NONE, done, retval = -EIO);

 done:
    return retval;
}

