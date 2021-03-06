/*
Copyright (C) 2012 Daniel Hazelbaker

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "client.h"
#include "common.h"
#include "conf.h"
#include "keys.h"
#include "ldap.h"
#include "listener.h"
#include "pwdb.h"
#include "sasl_auxprop.h"
#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <sasl/sasl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int doExit = 0;

const char *myHostname = NULL;
const char *myAddress = NULL;

static void usage();

//
// Catch signals that we should exit for.
//
void terminate(int signum) {
    fprintf(stderr, "Got signal %d, exiting...\r\n", signum);
    doExit = 1;
}

//
// Retrieve an SASL option.
//
static int getopt_func(void *context, const char *plugin_name,
                       const char *option, const char **result, unsigned *len) {
    const char *value;
    char option_name[256];

    //
    // Construct a more helpful option name.
    //
    if (plugin_name != NULL)
        snprintf(option_name, sizeof(option_name) - 1, "sasl_%s_%s",
                 plugin_name, option);
    else
        snprintf(option_name, sizeof(option_name) - 1, "sasl_%s", option);
    option_name[sizeof(option_name) - 1] = '\0';

#ifdef DEBUG
//    printf("CyrusOption: %s\r\n", option_name);
#endif

    //
    // Get the value for this option.
    //
    value = conf_find(option_name);
    if (value != NULL) {
        *result = value;

        return SASL_OK;
    }

    //
    // If the value was not found and it is from the lpws_ldap plugin, then
    // to see if we have a generic version of the same.
    //
    if (plugin_name != NULL && strcasecmp(plugin_name, "lpws_ldap") == 0) {
        snprintf(option_name, sizeof(option_name) - 1, "ldap_%s", option);
        value = conf_find(option_name);
        if (value != NULL) {
            *result = value;

            return SASL_OK;
        }
    }

    return SASL_FAIL;
}

//
// Log a message from the SASL system.
//
static int log_func(void *context, int level, const char *message) {
#ifdef DEBUG
    printf("CyrusLog: %s\r\n", message);
#endif

    return SASL_OK;
}

static sasl_callback_t callbacks[] = {
    {SASL_CB_GETOPT, (int (*)()) & getopt_func, NULL},
    {SASL_CB_LOG, (int (*)()) & log_func, NULL},
    {SASL_CB_LIST_END, NULL, NULL}};

static struct option longopts[] = {{"config", required_argument, NULL, 'c'},
                                   {"update", no_argument, NULL, 'u'},
                                   {"force", no_argument, NULL, 'f'},
                                   {"help", no_argument, NULL, 'h'},
                                   {"adduser", required_argument, NULL, 'n'},
                                   {"deleteuser", required_argument, NULL, 'd'},
                                   {NULL, 0, NULL, 0}};

int main(int argc, char *argv[]) {
    const char *config_file = "/etc/passwdd.conf";
    const char *add_username = NULL;
    const char *delete_username = NULL;
    int ch, updateAuth = 0, force = 0;

    while ((ch = getopt_long(argc, argv, "c:ufhn:", longopts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            config_file = optarg;
            break;

        case 'u':
            updateAuth = 1;
            break;

        case 'f':
            force = 1;
            break;

        case 'n':
            add_username = optarg;
            break;

        case 'd':
            delete_username = optarg;
            break;

        case 'h':
        default:
            usage();
        }
    }

    if (conf_init(config_file) == -1)
        exit(1);

    //
    // Get the hostname and primary IP address.
    //
    char *name, *address;

    if (conf_find("hostname") != NULL) {
        name = strdup(conf_find("hostname"));
    } else {
        name = malloc(256);
        if (gethostname(name, 256) != 0)
            exit(1);
    }
    myHostname = name;

    if (conf_find("ipaddress") != NULL) {
        address = strdup(conf_find("ipaddress"));
    } else {
        struct hostent *ent = gethostbyname(myHostname);
        if (ent == NULL)
            exit(1);
        struct in_addr **addrs = (struct in_addr **)ent->h_addr_list;
        if (addrs[0] == NULL)
            exit(1);
        address = strdup(inet_ntoa(addrs[0][0]));
    }
    myAddress = address;

    printf("Running with local address %s:%s.\r\n", myHostname, myAddress);

    //
    // Catch a kill and CTRL-C.
    //
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);

    if (loadKeys() == -1)
        exit(1);
    if (pwdb_open() != 0)
        exit(1);

    //
    // Make sure all the user records have a authAuthority record for us.
    //
    //if (updateAuth == 1) {
    //    ldap_updateAuthority(force);
    //    pwdb_close();
    //    exit(0);
    //}

    //
    // Add a user from the command line.
    //
    //if (add_username != NULL) {
    //    char *password;
    //
    //    password = getpass("New Password: ");
    //    if (pwdb_adduser(add_username, password, 0) != 0)
    //        printf("Failed to add new user.\r\n");
    //    pwdb_close();
    //
    //    exit(0);
    //}

    //
    // Delete a user from the command line.
    //
    //if (delete_username != NULL) {
    //    if (pwdb_deleteuser(delete_username) != 0)
    //        printf("Failed to delete user.\r\n");
    //    pwdb_close();
    //
    //    exit(0);
    //}

    client_init();

    if (sasl_server_init(callbacks, "passwdd") != SASL_OK) {
        printf("Failed to initialize SASL.\r\n");
        pwdb_close();
        exit(1);
    }

    if (sasl_auxprop_add_plugin("lpws_internal", lpws_internal_auxprop_init) != SASL_OK) {
        printf("Failed to load internal SASL plugin.\r\n");
        pwdb_close();
        exit(1);
    }

    if (listeners_setup() == -1) {
        printf("Failed to setup server sockets.\r\n");
        pwdb_close();
        exit(1);
    }

    while (!doExit) {
        //if (listeners_poll() == -1) {
        //    printf("Something very bad happened polling for activity. Aborting.\r\n");
        //    exit(2);
        //}
        if (listeners_kqueue() != 0) {
            printf("Something very bad happened while processing activity. Aborting.\r\n");
            exit(2);
        }
    }

    //
    // Close all client sockets.
    //

    //
    // Close all server sockets.
    //
    listeners_close();

    //
    // Close database.
    //
    pwdb_close();

    return 0;
}

//
// Display some simple usage information to the user.
//
static void usage() {
    printf("Usage:\r\n");
    printf("\tpasswdd [-c config]\r\n");
    printf("\tpasswdd [-c config] -u [-f]\r\n");
    printf("\tpasswdd [-c config] --adduser <username>\r\n");
    printf("\tpasswdd [-c config] --deleteuser <username>\r\n");
    exit(-1);
}
