<?xml?>
<class
    name      = "amq_dispatch"
    comment   = "Dispatched message class"
    version   = "1.0"
    copyright = "Copyright (c) 2004 JPMorgan"
    script    = "icl_gen"
    >

<inherit class = "ipr_list_item" />
<option name = "nullify" value = "1" />

<import class = "amq_handle"  />
<import class = "ipr_classes" />
<public name = "header">
#include "amq_core.h"
#include "amq_frames.h"
</public>

<context>
    /*  References to parent objects                                         */
    amq_consumer_t
        *consumer;                      /*  Parent consumer                  */
    amq_channel_t
        *channel;                       /*  Parent channel                   */
    amq_handle_t
        *handle;                        /*  Parent handle for dispatch       */
    amq_queue_t
        *queue;                         /*  Parent queue for dispatch        */

    /*  Object properties                                                    */
    amq_db_t
        *db;                            /*  Database for virtual host        */
    amq_smessage_t
        *message;                       /*  Message dispatched               */
    qbyte
        mesg_id;                        /*  Message id                       */
    qbyte
        message_nbr;                    /*  Message id                       */
    Bool
        closed;                         /*  Finished processing?             */
</context>

<method name = "new">
    <argument name = "consumer" type = "amq_consumer_t *">Parent consumer</argument>
    <argument name = "message"  type = "amq_smessage_t *">Message object</argument>
    <argument name = "mesg_id"  type = "qbyte" >Message id, if persistent</argument>

    /*  De-normalise from parent object, for simplicity of use               */
    self->consumer    = consumer;
    self->handle      = consumer->handle;
    self->channel     = consumer->channel;
    self->queue       = consumer->queue;
    self->db          = consumer->db;

    /*  Initialise other properties                                          */
    self->message     = message;        /*  We now 0wn this message          */
    self->mesg_id     = mesg_id;
    self->message_nbr = ++(self->channel->message_nbr);

    amq_dispatch_list_queue (self->channel->dispatched, self);

    /*  Dispatched message decrements message windows                        */
    ASSERT (self->queue->window);
    ASSERT (self->consumer->window);
    self->queue->window--;
    self->consumer->window--;
</method>

<method name = "destroy">
    amq_smessage_destroy (&self->message);
</method>

<method name = "ack" template = "function">
    <doc>
    Acknowledge a specific message.
    </doc>

    /*  Queue and consumer can accept a new message                          */
    self->queue->window++;
    self->consumer->window++;
    amq_queue_dispatch (self->queue, NULL);

    if (self->channel->transacted) {
        /*  ***TODO*** use channel transaction if transacted
         */
        if (self->mesg_id)
            amq_db_mesg_delete_fast (self->db, self->mesg_id);
        self->closed = TRUE;
    }
    else {
        if (self->mesg_id)
            amq_db_mesg_delete_fast (self->db, self->mesg_id);
        amq_dispatch_destroy (&self);
    }
</method>

<method name = "unget" template = "function">
    <doc>
    Unget a specific message.
    </doc>
    <local>
    amq_db_mesg_t
        *mesg;
    </local>

    if (self->mesg_id == 0) {
        /*  Push back non-persistent message                                 */
        /*    - update window after so it won't bounce to same consumer      */
        amq_queue_dispatch (self->queue, self->message);
        self->queue->window++;
        self->consumer->window++;
    }
    else {
        mesg = amq_db_mesg_new ();
        mesg->id = self->mesg_id;
        amq_db_mesg_fetch (self->db, mesg, AMQ_DB_FETCH_EQ);

        mesg->client_id = 0;
        /*  ***TODO*** use channel transaction if transacted
        if (self->channel->transacted)
        */
        amq_db_mesg_update (self->db, mesg);

        /*  Reset last message id to cover this message                      */
        if (self->queue->last_mesg_id > mesg->id)
            self->queue->last_mesg_id = mesg->id - 1;
        amq_db_mesg_destroy (&mesg);

        /*  Queue and consumer can accept a new message                      */
        self->queue->window++;
        self->consumer->window++;
        amq_queue_dispatch (self->queue, NULL);
    }
    amq_dispatch_destroy (&self);
</method>

<method name = "selftest">
</method>

</class>
