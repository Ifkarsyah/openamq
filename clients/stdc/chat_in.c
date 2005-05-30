/*  -------------------------------------------------------------------------
 *
 *  chat_in - simple client, reads from 'c-in' queue.
 *
 *  Copyright (c) 1991-2005 iMatix Corporation
 *  Copyright (c) 2004-2005 JPMorgan Inc.
 *  -------------------------------------------------------------------------*/

#include "amq_core.h"
#include "amq_sclient.h"
#include "version.h"

#define CLIENT_NAME "Chat input window - OpenAMQ/" VERSION "\n"
#define NOWARRANTY \
"This is free software; see the source for copying conditions.  There is NO\n" \
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
#define USAGE                                                               \
    "Syntax: chat_in [options...]\n"                                     \
    "Options:\n"                                                            \
    "  -s server        Name or address of server (localhost)\n"            \
    "  -c clientname    Client identifier (default: 'stdc chat in')\n"      \
    "  -t level         Set trace level\n"                                  \
    "  -v               Show version information\n"                         \
    "  -h               Show summary of command-line options\n"             \
    "\nThe order of arguments is not important. Switches and filenames\n"   \
    "are case sensitive. See documentation for detailed information.\n"


int
main (int argc, char *argv [])
{
    int
        argn;                           /*  Argument number                  */
    Bool
        args_ok = TRUE;                 /*  Were the arguments okay?         */
    char
        *opt_client,                    /*  Client identifier                */
        *opt_server,                    /*  Host to connect to               */
        *opt_trace,                     /*  0-3                              */
        **argparm;                      /*  Argument parameter to pick-up    */
    amq_sclient_t
        *amq_client;
    dbyte
        in_handle = 0;
    amq_message_t
        *message;
    int
        length;
    ipr_shortstr_t
        text;

    /*  These are the arguments we may get on the command line               */
    opt_client   = "stdc chat in";
    opt_server   = "localhost";
    opt_trace    = "0";

    console_send     (NULL, TRUE);
    console_capture  ("chat_in.log", 'w');
    console_set_mode (CONSOLE_DATETIME);

    argparm = NULL;                     /*  Argument parameter to pick-up    */
    for (argn = 1; argn < argc; argn++) {
        /*  If argparm is set, we have to collect an argument parameter      */
        if (argparm) {
            if (*argv [argn] != '-') {  /*  Parameter can't start with '-'   */
                *argparm = strdupl (argv [argn]);
                argparm = NULL;
            }
            else {
                args_ok = FALSE;
                break;
            }
        }
        else
        if (*argv [argn] == '-') {
            switch (argv [argn][1]) {
                /*  These switches take a parameter                          */
                case 'c':
                    argparm = &opt_client;
                    break;
                case 's':
                    argparm = &opt_server;
                    break;
                case 't':
                    argparm = &opt_trace;
                    break;

                /*  These switches have an immediate effect                  */
                case 'v':
                    puts (CLIENT_NAME);
                    puts (COPYRIGHT);
                    puts (NOWARRANTY);
                    puts (BUILDMODEL);
                    puts ("Built on: " BUILDDATE);
                    exit (EXIT_SUCCESS);
                case 'h':
                    puts (CLIENT_NAME);
                    puts (COPYRIGHT);
                    puts (NOWARRANTY);
                    puts (USAGE);
                    exit (EXIT_SUCCESS);

                /*  Anything else is an error                                */
                default:
                    args_ok = FALSE;
            }
        }
        else {
            args_ok = FALSE;
            break;
        }
    }
    /*  If there was a missing parameter or an argument error, quit          */
    if (argparm) {
        coprintf ("Argument missing - type 'chat_in -h' for help");
        goto failed;
    }
    else
    if (!args_ok) {
        coprintf ("Invalid arguments - type 'chat_in -h' for help");
        goto failed;
    }
    amq_sclient_trace (atoi (opt_trace));

    coprintf ("Connecting to %s...", opt_server);
    amq_client = amq_sclient_new (opt_client, "guest", "guest");

    if (amq_client == NULL
    ||  amq_sclient_connect (amq_client, opt_server, "/test"))
        goto failed;

    /*  We ask the server to auto-acknowledge messages (unreliable)          */
    in_handle = amq_sclient_consumer (
        amq_client,
        AMQP_SERVICE_QUEUE,
        "chat-queue",
        1,                              /*  Prefetch size                    */
        FALSE,                          /*  No local                         */
        TRUE,                           /*  No acknowledgements              */
        TRUE);                          /*  Dynamic queue consumer           */

    while (TRUE) {
        message = amq_sclient_msg_read (amq_client, 0);
        amq_sclient_commit (amq_client);
        if (message) {
            length = amq_message_get_content (message, text, 255);
            text [length] = '\0';
            coprintf (text);
            amq_message_destroy (&message);
            if (streq (text, "bye"))
                break;
        }
        else
            break;
    }
    amq_sclient_close   (amq_client, 0);
    amq_sclient_destroy (&amq_client);
    icl_system_destroy ();
    icl_mem_assert ();
    return (0);

    failed:
        exit (EXIT_FAILURE);
}
