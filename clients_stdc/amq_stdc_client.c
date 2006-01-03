/*---------------------------------------------------------------------------
 *  amq_stdc_client.c - implementation of AMQ client API
 *
 *  Copyright (c) 2004-2005 JPMorgan
 *  Copyright (c) 1991-2005 iMatix Corporation
 *---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
 *  DESIGN SUMMARY
 *
 *  Implementation of AMQ client API is composed of several finite state
 *  machines (stateful, of course) plus a lightweight wrapper (stateless)
 *  that converts client requests as well as commands sent by server to state
 *  machine events. Wrapper converting client requests resides in
 *  amq_stdc_client.c, coversion of server commands is done by
 *  dispatcher thread (see s_receiver_thread in amq_stdc_connection_fsm.c).
 *  Thread synchronisation is done on state machine level: any state machine
 *  may be accessed only by a single thread at any given moment. Thus it is
 *  important for threads to spent as little time as possible within the
 *  state machine, not to block other threads that may require access to the
 *  same state machine. This means no blocking calls should be done within
 *  state machines. There are three types of blocking calls to consider.
 *  First we have reading from socket. This is done within dispatcher thread
 *  and only subsequently is received command converted to event and sent to
 *  the apropriate state machine. Next there is writing to socket. It may be
 *  blocking when socket buffer is full. Special sender thread would be an
 *  universal solution. (Not implemented yet.) Third type of blocking is when
 *  command is sent to server and response from server is required to
 *  continue. This problem is solved using concept of locks. Wrapper layer
 *  issues event which is processed on spot (command is sent to server) and
 *  lock is returned. Wrapper layer may wait for the lock, that is released
 *  once response from server is received. Reply data are passed at the same
 *  time from thread unlocking the lock to the thread waiting for the lock.
 *  Implementation of locks may be found in amq_stads_global_fsm.c.
 *  There are four state machines altogether within client API: global state
 *  machine (holds state common to the whole of API), connection state machine
 *  (holds state associated with connection), channel state machine (holds
 *  state associated with channel) and handle state machine (holds state
 *  associated with handle). There may be several connection FSM instances
 *  existing at the same time, several channel FSM instaces per connection and
 *  handle FSM instances per channel.
 *  State machine definitions are in amq_stdc_fsms.xml. Processing this file
 *  using amq_stdc_fsms.gsl generates <fsm-name>.i and <fsm-name>.d files for
 *  every state machine, which are in turn included into <fsm-name>.h and
 *  <fsm-name>.c. <fsm-name>.c contains implementation of actions of
 *  corresponding state machine.
 *---------------------------------------------------------------------------*/

#include "amq_stdc_private.h"
#include "amq_stdc_client.h"
#include "amq_stdc_global_fsm.h"

static global_fsm_t
    global;

/*  -------------------------------------------------------------------------
    Function: amq_stdc_init

    Synopsis:
    Initialises API module.
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_init ()
{
    apr_status_t
        result;

    /*  TODO: Number of initiations should be refcounted here and apr_init   */
    /*  should be called, but there's a vicious circle here:                 */
    /*  apr initialization isn't thread safe so we have to make amq_init     */
    /*  threadsafe... to do it we need a mutex, but apr isn't initialised    */
    /*  yet so we cannot create a mutex...                                   */
    result = global_fsm_create (&global);
    AMQ_ASSERT_STATUS (result, global_fsm_create)
    result = global_fsm_init (global);
    AMQ_ASSERT_STATUS (result, global_fsm_init)
 
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_term

    Synopsis:
    Deinitialises API module. If there are objects still opened attempts to
    shut them down gracefuly.
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_term ()
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    /*  TODO: See TODO note in amq_stdc_init                                 */
    result = global_fsm_terminate (global, &lock);
    AMQ_ASSERT_STATUS (result, global_fsm_terminate)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    result = global_fsm_destroy (global);
    AMQ_ASSERT_STATUS (result, global_fsm_destroy)
    amq_stats ();
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_open_connection

    Synopsis:
    Creates new connection object and opens a connection to server.

    Arguments:
        server                server to connect to
        port                  port to use
        host                  virtual host to connect to
        client_name           client name to use when connecting
        out_heartbeat_model   specifies how heartbeats are to be sent to server
        in_heartbeat_mode     specifies how heartbeats are to be received
                              from server
        in_heartbeat_interval interval in which client wants server to send
                              heartbeats
        options_size          size of options table
        options               options table passed to CONNECTION OPEN command  
        async                 if 1, doesn't wait for confirmation
        connection            out parameter; new connection object
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_open_connection (
    const char                  *server,
    dbyte                       port,
    const char                  *host,
    const char                  *client_name,
    amq_stdc_heartbeat_model_t  out_heartbeat_model,
    amq_stdc_heartbeat_model_t  in_heartbeat_model,
    apr_interval_time_t         in_heartbeat_interval,
    dbyte                       options_size,
    const char                  *options,
    byte                        async,
    amq_stdc_connection_t       *out
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = global_fsm_create_connection (global, server, port, host,
        client_name, options_size, options, async, out, &lock);
    AMQ_ASSERT_STATUS (result, global_fsm_create_connection);
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock);

    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_close_connection

    Synopsis:
    Closes connection to server.

    Arguments:
        connection          connection object to close
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_close_connection (
    amq_stdc_connection_t  context
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = connection_fsm_terminate (context, &lock);
    AMQ_ASSERT_STATUS (result, connection_fsm_terminate)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    result = connection_fsm_destroy (context);
    AMQ_ASSERT_STATUS (result, connection_fsm_destroy)
 
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_open_channel

    Synopsis:
    Opens channel.

    Arguments:
        connection          parent connection for the channel
        transacted          whether channel is to be transacted
        restartable         whether channel supports restartable messages
        options             options table to be passed to CHANNEL OPEN
        out_of_band         specifies how out of band transfer should work
        async               if 1, don't wait for confirmation
        channel             output parameter; new channel object
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_open_channel (
    amq_stdc_connection_t  context,
    byte                   transacted,
    byte                   restartable,
    dbyte                  options_size,
    const char             *options,
    const char             *out_of_band,
    byte                   async,
    amq_stdc_channel_t     *out)
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = connection_fsm_create_channel (context, transacted, restartable,
        options_size, options, out_of_band, async, out, &lock);
    AMQ_ASSERT_STATUS (result, connection_fsm_create_channel);
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock);

    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_open_handle

    Synopsis:
    Opens handle.

    Arguments:
        channel               parent channel for the handle
        producer              if 1, client is allowed to send messages
        consumer              if 1, client is allowed to receive messages
        browser               if 1, client is allowed to browse for messages
        mime_type             MIME type
        encoding              content encoding
        options_size          options table size
        options               options table passed to HANDLE OPEN command
        async                 if 1, doesn't wait for confirmation
        handle_id             out parameter; newly allocated handle id
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_open_handle (
    amq_stdc_channel_t       context,
    byte                     producer,
    byte                     consumer,
    byte                     browser,
    const char               *mime_type,
    const char               *encoding,
    dbyte                    options_size,
    const char               *options,
    byte                     async,
    dbyte                    *handle_id
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_open_handle (context, producer,
        consumer, browser, mime_type, encoding, options_size, options, async,
        handle_id, &lock);
    AMQ_ASSERT_STATUS (result, channel_fsm_create_handle);

    /*  Wait for confirmation                                                */
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock);

    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_close_handle

    Synopsis:
    Closes handle.

    Arguments:
        channel              channel object
        handle_id            id of handle to close
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_close_handle (
    amq_stdc_channel_t  context,
    dbyte               handle_id
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_close_handle (context, handle_id, &lock);
    AMQ_ASSERT_STATUS (result, handle_fsm_terminate)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_acknowledge

    Synopsis:
    Acknowledges message(s).

    Arguments:
        channel             channel to do acknowledge on
        message_nbr         highest message number to be acknowledged
        async               if 1, don't wait for confirmation
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_acknowledge (
    amq_stdc_channel_t  context,
    qbyte               message_nbr,
    byte                async
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_acknowledge (context, message_nbr, async, &lock);
    AMQ_ASSERT_STATUS (result, channel_fsm_acknowledge)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_commit

    Synopsis:
    Commits work done.

    Arguments:
        channel             channel to do commit on
        options_size        size of options table
        options             options table to be passed to CHANNEL COMMIT
        async               if 1, don't wait for confirmation
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_commit (
    amq_stdc_channel_t  context,
    dbyte               options_size,
    const char          *options,
    byte                async
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_commit (context, options_size, options, async, &lock);
    AMQ_ASSERT_STATUS (result, channel_fsm_commit)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_rollback

    Synopsis:
    Rolls back work done.

    Arguments:
        channel             channel to do rollback on
        options_size        size of options table
        options             options table to be passed to CHANNEL ROLLBACK
        async               if 1, don't wait for confirmation
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_rollback (
    amq_stdc_channel_t  context,
    dbyte               options_size,
    const char          *options,
    byte                async
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_rollback (context, options_size, options, async,
        &lock);
    AMQ_ASSERT_STATUS (result, channel_fsm_rollback)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_send_message

    Synopsis:
    Sends message to server.

    Arguments:
        channel               channel handle
        handle_id             id of handle to use
        service_type          queue/topic/peer
        out_of_band           if 1, message data are to be transferred
                              out of band
        recovery              if 1, this is a recovery - only a partial message
        dest_name             destination name
        persistent            if 1, message is going to be persistent
        immediate             if 1, assert that the destination has consumers
        warning_tag           if not zero, warning is sent on minor errors
                              instead of closing the channel
        priority              priority of message
        expiration            expiration time of message
        mime_type             MIME type
        encoding              content encoding
        identifier            durable subscription name        
        headers_size          size of message headers table
        headers               message headers table
        data_size             number of bytes to send
        data                  position from which to read data
        async                 if 1, doesn't wait for confirmation
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_send_message (
    amq_stdc_channel_t       context,
    dbyte                    handle_id,
    amq_stdc_service_type_t  service_type,
    byte                     out_of_band,
    byte                     recovery,
    const char               *dest_name,
    byte                     persistent,
    byte                     immediate, 
    qbyte                     warning_tag,
    byte                     priority,
    qbyte                    expiration,
    const char               *mime_type,
    const char               *encoding,
    const char               *identifier,
    dbyte                    headers_size,
    const char               *headers,
    apr_size_t               data_size,
    void                     *data,
    byte                     async
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_send_message (context, handle_id, service_type,
        out_of_band, recovery, dest_name, persistent, immediate, warning_tag,
        priority, expiration, mime_type, encoding, identifier, headers_size,
        headers, data_size, data, async, &lock);
    AMQ_ASSERT_STATUS (result, handle_fsm_send_message)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_consume

    Synopsis:
    Starts message consumption.

    Arguments:
        channel               channel handle
        handle_id             id of handle to use
        service_type          queue/topic/peer
        prefetch              number of messages to prefetch
        no_local              if 1, messages sent from this connection won't be
                              received
        no_ack                if 1, client won't acknowledge messages
        dynamic               if 1, create destination if it doesn't exist
        dest_name             destination name
        selector_size         size of selector 
        selector              selector table
        async                 if 1, doesn't wait for confirmation
        dest_name_out         out parameter; name of newly created dynamic
                              destination; filled only when dynamic = 1 and
                              dest_name = ""
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_consume (
    amq_stdc_channel_t       context,
    dbyte                    handle_id,
    amq_stdc_service_type_t  service_type,
    dbyte                    prefetch,
    byte                     no_local,
    byte                     no_ack,
    byte                     dynamic,
    const char               *dest_name,
    dbyte                    selector_size,
    const char               *selector,
    byte                     async,
    char                     **dest_name_out
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;
    amq_stdc_lock_t
        created_lock;

    result = channel_fsm_consume (context, handle_id, service_type, prefetch,
        no_local, no_ack, dynamic, dest_name, selector_size, selector, async,
        &created_lock, &lock);
    AMQ_ASSERT_STATUS (result, handle_fsm_consume)

    /*  Wait for HANDLE CREATED                                              */
    result = wait_for_lock (created_lock, (void**) dest_name_out);
    AMQ_ASSERT_STATUS (result, wait_for_lock);

    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_get_message

    Synopsis:
    Returns one message received from server to the caller

    Arguments:
        channel             channel handle
        wait                if 1, API will wait till message arrives from
                            server
                            if 0, returns message only if it is already present
                            otherwise returns NULL in message_desc and
                            message parameters
        warning             out parameter; 0 if it is a message, 1 if it is
                            a warning
        message_desc        out parameter; pointer to structure describing
                            the message
        message             out parameter; message returned
        warning_tag         out parameter; warning tag in case it is a warning
    -------------------------------------------------------------------------*/

typedef struct
{
    amq_stdc_message_desc_t
        desc;
    message_fsm_t
        message;
    byte
        warning;
    qbyte
        warning_tag;
} msg_transfer_struct_t;

apr_status_t amq_stdc_get_message (
    amq_stdc_channel_t       channel,
    byte                     wait,
    byte                     *warning,
    amq_stdc_message_desc_t  **message_desc,
    amq_stdc_message_t       *message,
    qbyte                    *warning_tag
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;
    char
        *msg;
    msg_transfer_struct_t
        *ts;

    result = channel_fsm_get_message (channel, wait, warning, message_desc,
        message, warning_tag, &lock);
    AMQ_ASSERT_STATUS (result, channel_fsm_get_message)
    if (wait) {
        result = wait_for_lock (lock, (void**) &msg);
        AMQ_ASSERT_STATUS (result, wait_for_lock)
        if (msg) {
            ts = (msg_transfer_struct_t*) msg;
            if (message)
                *message = ts->message;
            if (message_desc)
                *message_desc = &(ts->desc);
            if (warning)
                *warning = ts->warning;
            if (warning_tag)
                *warning_tag = ts->warning_tag;
        }
        else {
            if (message)
                *message = NULL;
            if (message_desc)
                *message_desc = NULL;
            if (warning)
                *warning = 0;
            if (warning_tag)
                *warning_tag = 0;
        }
    }

    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_browse

    Synopsis:
    Browses for message.

    Arguments:
        channel             channel handle
        handle_id           id of handle to use
        message_nbr         message to browse
        async               if 0, API waits while message is delivered
                            if 1, sets message and message_desc to NULL and
                            returns message via standard amq_stdc_get_message
                            function
        message_desc        out parameter; pointer to structure describing
                            the message
        message             out parameter; message returned
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_browse (
    amq_stdc_channel_t       context,
    dbyte                    handle_id,
    qbyte                    message_nbr,
    byte                     async,
    amq_stdc_message_desc_t  **message_desc,
    amq_stdc_message_t       *message
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;
    char
        *msg;
 
    result = channel_fsm_browse (context, handle_id, message_nbr, async,
        &lock);
    AMQ_ASSERT_STATUS (result, handle_fsm_browse)
    result = wait_for_lock (lock, (void**) &msg);
    AMQ_ASSERT_STATUS (result, wait_for_lock)

    if (!msg) {
        if (message)
            *message = NULL;
        if (message_desc)
            *message_desc = NULL;
    }
    else {
        if (message)
            *message = *((amq_stdc_message_t*)
                (msg + sizeof (amq_stdc_message_desc_t)));
        if (message_desc)
            *message_desc = (amq_stdc_message_desc_t*) msg;
    }
   
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_flow

    Synopsis:
    Suspends or restards message flow.

    Arguments:
        channel               channel handle
        handle_id             id of handle to use
        pause                 if 1, suspend, if 0, restart
        async                 if 1, doesn't wait for confirmation
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_flow (
    amq_stdc_channel_t  context,
    dbyte               handle_id,
    byte                pause,
    byte                async
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_flow (context, handle_id, pause, async, &lock);
    AMQ_ASSERT_STATUS (result, handle_fsm_flow)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_unget_message

    Synopsis:
    Returns a message to server

    Arguments:
        channel               channel handle
        handle_id             id of handle to use
        message_nbr           number of message to return          
        async                 if 1, doesn't wait for confirmation
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_unget_message (
    amq_stdc_channel_t  context,
    dbyte               handle_id,
    qbyte               message_nbr,
    byte                async
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_unget (context, handle_id, message_nbr, async, &lock);
    AMQ_ASSERT_STATUS (result, message_fsm_unget)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_query

    Synopsis:
    Issues a query to server.

    Arguments:
        channel             channel handle
        handle_id           id of handle to use
        message_nbr         request for messages with number over this value;
                            0 means all messages
        dest_name           destination name
        selector_size       size of selector parameter
        selector            selector string
        mime_type           MIME type
        partial             if 1 reads only single batch of results from server
        resultset           out parameter; returned resultset
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_query (
    amq_stdc_channel_t  context,
    dbyte               handle_id,
    qbyte               message_nbr,
    const char          *dest_name,
    dbyte               selector_size,
    const char          *selector,
    const char          *mime_type,
    byte                partial,
    char                **resultset
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = get_exclusive_access_to_query_dialogue (context);
    AMQ_ASSERT_STATUS (result, get_exclusive_access_to_query_dialogue);
    result = channel_fsm_query (context, handle_id, message_nbr, dest_name,
        selector_size, selector, mime_type, &lock);
    AMQ_ASSERT_STATUS (result, handle_fsm_query)
    result = wait_for_lock (lock, (void**) resultset);
    AMQ_ASSERT_STATUS (result, wait_for_lock)

    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_close_channel

    Synopsis:
    Closes channel.

    Arguments:
        channel             channel object to close
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_close_channel (
    amq_stdc_channel_t  context
    )
{
    apr_status_t
        result;
    amq_stdc_lock_t
        lock;

    result = channel_fsm_terminate (context, &lock);
    AMQ_ASSERT_STATUS (result, channel_fsm_terminate)
    result = wait_for_lock (lock, NULL);
    AMQ_ASSERT_STATUS (result, wait_for_lock)
    result = channel_fsm_destroy (context);
    AMQ_ASSERT_STATUS (result, channel_fsm_destroy)
    
    return APR_SUCCESS;
}

/*  -------------------------------------------------------------------------
    Function: amq_stdc_destroy_query

    Synopsis:
    Deallocates resources associated with query result.

    Arguments:
        query                query resultset to destroy
    -------------------------------------------------------------------------*/

apr_status_t amq_stdc_destroy_query (
    char  *query
    )
{
    amq_free ((void*) query);
    return APR_SUCCESS;
}

/*---------------------------------------------------------------------------*/

size_t amq_stdc_open_inpipe (
    amq_stdc_message_t  message,
    amq_stdc_inpipe_t   *inpipe
    )
{
    if (inpipe)
        *inpipe = (amq_stdc_inpipe_t) message;

    return APR_SUCCESS;
}

size_t amq_stdc_open_outpipe (
    amq_stdc_message_t  message,
    amq_stdc_outpipe_t   *outpipe
    );

size_t amq_stdc_open_stream (
    amq_stdc_message_t  message,
    amq_stdc_stream_t   *stream
    );


apr_status_t amq_stdc_close_message (
    amq_stdc_message_t  message,
    byte                async
    )
{
    apr_status_t
        result;

    result = message_fsm_terminate (message);
    AMQ_ASSERT_STATUS (result, message_fsm_read)

    return APR_SUCCESS;
}

/*---------------------------------------------------------------------------*/

size_t amq_stdc_pread (
    amq_stdc_inpipe_t  inpipe,
    void               *destination,
    size_t             size,
    byte               wait,
    byte               complete
    )
{
    apr_status_t
        result;
    qbyte
        out_size;
    amq_stdc_lock_t
        lock;
    void
        *out;

    result = message_fsm_pread ((amq_stdc_message_t) inpipe, destination, size,
        wait, complete, &out_size, &lock);
    AMQ_ASSERT_STATUS (result, message_fsm_read)

    if (wait && lock) {
        result = wait_for_lock (lock, &out);
        AMQ_ASSERT_STATUS (result, wait_for_lock)
        out_size = (qbyte) out;
    }

    return out_size;
}

size_t amq_stdc_pskip (
    amq_stdc_inpipe_t  inpipe,
    size_t             size,
    byte               wait,
    byte               complete
    )
{
    apr_status_t
        result;
    qbyte
        out_size;
    amq_stdc_lock_t
        lock;
    void
        *out;

    result = message_fsm_pread ((amq_stdc_message_t) inpipe, NULL, size,
        wait, complete, &out_size, &lock);
    AMQ_ASSERT_STATUS (result, message_fsm_read)

    if (wait && lock) {
        result = wait_for_lock (lock, &out);
        AMQ_ASSERT_STATUS (result, wait_for_lock)
        out_size = (qbyte) out;
    }

    return out_size;
}

byte amq_stdc_peof (
    amq_stdc_inpipe_t  inpipe
    )
{
    apr_status_t
        result;
    byte
        out;

    result = message_fsm_peof ((amq_stdc_message_t) inpipe, &out);
    AMQ_ASSERT_STATUS (result, message_fsm_peof)

    return out;
}