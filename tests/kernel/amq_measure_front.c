#include "asl.h"
#include "amq_client_connection.h"
#include "amq_client_session.h"

int main (int argc, char** argv) 
{
    amq_client_connection_t
        *connection = NULL;             //  Current connection
    amq_client_session_t
        *session = NULL;                //  Current session
    
    amq_content_jms_t
        *message_tx = NULL,             //  Current sent message
        *message_rx = NULL;             //  Current received message
    
    icl_shortstr_t
        message_id;                     //  Current message ID
    
    icl_longstr_t
        *arguments = NULL;              //  Arguments for bind
    byte
        *payload = NULL;                //  The message payload

    int
        payload_size = 1024,            //  Size of the message
        publish_result,                 //  Result of the message publication
        loopcount;                      //  Counter for loop
        
    
    //  Initialize system in order to use console
    icl_system_initialise (argc, argv);

    //  Set up a connection
    connection = amq_client_connection_new ("localhost:9876",
                                            amq_client_connection_auth_plain("guest","guest"),
                                            0,      //  Trace level
                                            30000); //  Timeout

    if (connection) {
        icl_console_print("I: connected to: %s/%s",
                          connection->server_product,
                          connection->server_version);
    } else {
        icl_console_print("E: could not connect to server"); 
        goto finished;
    }


    //  Set up a session
    session = amq_client_session_new (connection,"/");

    if (!session) {
        icl_console_print("E: could not open session to server");
        goto finished;
    }

    //  Declare a queue
    amq_client_session_queue_declare (session,      //  The session
                                      0,            //  Access ticked granted by server
                                      "global",     //  Queue scope
                                      "",           //  Queue name
                                      FALSE,        //  Passive declaration
                                      FALSE,        //  Durable declaration
                                      FALSE,        //  Private queue
                                      TRUE);        //  Auto delete

    //  Bind the queue to the exchange 
    arguments = asl_field_list_build ("destination", session->queue_name, NULL);
    amq_client_session_queue_bind (session,
                                   0,                    //  Ticket
                                   "global",             //  Queue domain
                                   session->queue_name,  //  Queue
                                   "$queue",             //  Exchange 
                                   arguments);           //  Arguments
    icl_longstr_destroy (&arguments);
    
    amq_client_session_jms_consume (session,
                                    0,                    //  Access ticket granted by server
                                    "global",             //  Queue scope
                                    session->queue_name,  //  Queue name
                                    0,                    //  Prefetch size
                                    0,                    //  Prefetch count
                                    FALSE,                //  No local messages
                                    TRUE,                 //  Auto-acknowledge
                                    FALSE);               //  Exclusive access to queue


    //  Set the message's payload
    payload = icl_mem_alloc (payload_size);
    memset (payload, 0xAB, payload_size);

    loopcount=0;
    //  Sent it over to the server
    for(;;loopcount++) {    
        //  Create a message structure
        message_tx = amq_content_jms_new ();
    

        //  Setup a message_id 
        icl_shortstr_fmt (message_id, "ID%d", loopcount);

        //  Insert payload and message_id into the message
        amq_content_jms_set_body (message_tx, payload, payload_size, NULL);
        amq_content_jms_set_message_id (message_tx, message_id);
        amq_content_jms_set_reply_to (message_tx, session->queue_name);

        publish_result = amq_client_session_jms_publish (session,
                                                         message_tx,
                                                         0,             //  Ticket
                                                         "$queue",      //  Exchange
                                                         "service",     //  Destination
                                                         FALSE,         //  Mandatory
                                                         FALSE);        //  Immediate
        if (publish_result != 0) {
            icl_console_print ("E: [%s] could not send message to server - %s",
                               "service",
                               connection->error_text);
            goto finished; 
        } else {
            icl_console_print ("I: [%s] message {%s} sent to server",
                               "service",
                               message_tx->message_id);
         }

        amq_client_session_wait (session, 0);
        message_rx = amq_client_session_jms_arrived (session);
        
        if(message_rx != NULL) {
            icl_console_print("I: [%s] message received from server with id {%s}",
                              session->queue_name,
                              message_rx->message_id);
            amq_content_jms_destroy (&message_rx);
        } else {
            icl_console_print("I: no msg received, this should not happen, exitting");
            exit (-1);
        }
        
    //    sleep (1); 

    }
    //  Clean up
    finished:
    amq_client_session_destroy (&session);
    amq_client_connection_destroy (&connection);

    icl_mem_free (payload);
    icl_system_terminate();
    return 0;
}