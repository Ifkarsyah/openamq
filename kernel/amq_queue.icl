<?xml?>
<class
    name      = "amq_queue"
    comment   = "Polymorphic queue class"
    version   = "1.0"
    script    = "smt_object_gen"
    copyright = "Copyright (c) 2004-2005 iMatix Corporation"
    target    = "smt"
    >
<doc>
This class implements the server queue class, an asynchronous object
that acts as a envelope for the separate queue managers for each
class.  This is a lock-free asynchronous class.
</doc>

<inherit class = "smt_object" />
<inherit class = "ipr_hash_item">
    <option name = "hash_type" value = "str" />
    <option name = "hash_size" value = "255" />
</inherit>
<inherit class = "ipr_list_item">
    <option name = "prefix" value = "list" />
</inherit>

<import class = "amq_server_classes" />

<context>
    amq_vhost_t
        *vhost;                         //  Parent virtual host
    ipr_shortstr_t
        domain;                         //  Queue domain
    ipr_shortstr_t
        name;                           //  Queue name
    Bool
        durable,                        //  Is queue durable?
        private,                        //  Is queue private?
        auto_delete,                    //  Auto-delete unused queue?
        dirty;                          //  Queue has to be dispatched?
    amq_queue_jms_t
        *queue_jms;                     //  JMS content queue
    amq_queue_basic_t
        *queue_basic;                   //  Basic content queue
</context>

<method name = "new">
    <argument name = "vhost"    type = "amq_vhost_t *">Parent vhost</argument>
    <argument name = "domain"   type = "char *">Queue domain</argument>
    <argument name = "name"     type = "char *">Queue name</argument>
    <argument name = "durable"  type = "Bool">Is queue durable?</argument>
    <argument name = "private"  type = "Bool">Is queue private?</argument>
    <argument name = "auto delete" type = "Bool">Auto-delete unused queue?</argument>
    <dismiss argument = "table" value = "vhost->queue_table" />
    <dismiss argument = "key"   value = "self_fullname (domain, name, fullname)">
        Hash key is fullname formatted from queue domain plus name
    </dismiss>
    <local>
    ipr_shortstr_t
        fullname;
    </local>
    //
    self->vhost       = vhost;
    self->durable     = durable;
    self->private     = private;
    self->auto_delete = auto_delete;
    self->queue_jms   = amq_queue_jms_new   (self);
    self->queue_basic = amq_queue_basic_new (self);

    ipr_shortstr_cpy (self->domain, domain);
    ipr_shortstr_cpy (self->name,   name);
</method>

<method name = "destroy">
    <action>
    amq_queue_jms_destroy   (&self->queue_jms);
    amq_queue_basic_destroy (&self->queue_basic);
    </action>
</method>

<method name = "fullname" return = "fullname">
    <doc>
    Formats a full internal queue name based on the supplied domain
    and queue name.
    </doc>
    <argument name = "domain"   type = "char *">Name space</argument>
    <argument name = "name"     type = "char *">Queue name</argument>
    <argument name = "fullname" type = "char *">Result to format</argument>
    //
    ipr_shortstr_fmt (fullname, "%s|%s", domain? domain: "", name);
</method>
    
<method name = "search" return = "self">
    <argument name = "table"  type = "$(selfname)_table_t *">Queue table</argument>
    <argument name = "domain" type = "char *"               >Queue domain</argument>
    <argument name = "name"   type = "char *"               >Queue name</argument>
    <declare  name = "self" type = "$(selftype) *">The found object</declare>
    <local>
    ipr_shortstr_t
        fullname;
    </local>
    //
    self_fullname (domain, name, fullname);
    self = $(selfname)_table_search (table, fullname);
</method>

<method name = "publish" template = "async function" async = "1">
    <doc>
    Publish message content onto queue.
    </doc>
    <argument name = "channel"   type = "amq_server_channel_t *">Channel for reply</argument>
    <argument name = "class id"  type = "int">The content class</argument>
    <argument name = "content"   type = "void *">Message content</argument>
    <argument name = "immediate" type = "Bool">Warn if no consumers?</argument>
    //
    if (class_id == AMQ_SERVER_JMS)
        amq_content_jms_possess (content);
    else
    if (class_id == AMQ_SERVER_BASIC)
        amq_content_basic_possess (content);
    //
    <action>
    if (class_id == AMQ_SERVER_JMS) {
        amq_queue_jms_publish (self->queue_jms, channel, content, immediate);
        amq_content_jms_destroy ((amq_content_jms_t **) &content);
    }
    else
    if (class_id == AMQ_SERVER_BASIC) {
        amq_queue_basic_publish (self->queue_basic, channel, content, immediate);
        amq_content_basic_destroy ((amq_content_basic_t **) &content);
    }
    </action>
</method>

<method name = "browse" template = "async function" async = "1">
    <doc>
    Returns next message off queue, if any.
    </doc>
    <argument name = "channel"  type = "amq_server_channel_t *">Channel for reply</argument>
    <argument name = "class id" type = "int" >The content class</argument>
    //
    <action>
    if (class_id == AMQ_SERVER_JMS)
        amq_queue_jms_browse (self->queue_jms, channel);
    else
    if (class_id == AMQ_SERVER_BASIC)
        amq_queue_basic_browse (self->queue_basic, channel);
    else
        icl_console_print ("E: illegal content class (%d)", class_id);
    </action>
</method>

<method name = "consume" template = "async function" async = "1">
    <doc>
    Attach consumer to appropriate queue consumer list.
    </doc>
    <argument name = "consumer" type = "amq_consumer_t *">Consumer reference</argument>
    <argument name = "active"   type = "Bool">Create active consumer?</argument>
    //
    <action>
    if (consumer->class_id == AMQ_SERVER_JMS)
        amq_queue_jms_consume (self->queue_jms, consumer, active);
    else
    if (consumer->class_id == AMQ_SERVER_BASIC)
        amq_queue_basic_consume (self->queue_basic, consumer, active);

    //  Caller linked to the consumer on our behalf
    amq_consumer_unlink (&consumer);
    </action>
</method>

<method name = "cancel" template = "async function" async = "1">
    <doc>
    Cancel consumer, by reference.
    </doc>
    <argument name = "consumer" type = "amq_consumer_t *" ref = "1">Consumer reference</argument>
    //
    <action>
    if (consumer->class_id == AMQ_SERVER_JMS)
        amq_queue_jms_cancel (self->queue_jms, consumer);
    else
    if (consumer->class_id == AMQ_SERVER_BASIC)
        amq_queue_basic_cancel (self->queue_basic, consumer);
    </action>
</method>

<method name = "flow" template = "async function" async = "1">
    <doc>
    Pause or restart consumer.
    </doc>
    <argument name = "consumer" type = "amq_consumer_t *">Consumer</argument>
    <argument name = "active"   type = "Bool">Active consumer?</argument>
    //
    <action>
    if (consumer->class_id == AMQ_SERVER_JMS)
        amq_queue_jms_flow (self->queue_jms, consumer, active);
    else
    if (consumer->class_id == AMQ_SERVER_BASIC)
        amq_queue_basic_flow (self->queue_basic, consumer, active);

    //  Caller linked to the consumer on our behalf
    amq_consumer_unlink (&consumer);
    </action>
</method>

<method name = "dispatch" template = "async function" async = "1">
    <doc>
    Dispatches all pending messages waiting on the specified message queue.
    </doc>
    //
    <action>
    amq_queue_jms_dispatch   (self->queue_jms);
    amq_queue_basic_dispatch (self->queue_basic);
    self->dirty = FALSE;                //  Queue has been dispatched
    </action>
</method>

<method name = "pre dispatch" template = "function">
    <doc>
    Flags the queue as "dirty" and moves it to the front of the dispatch
    list so that the virtual host will dispatch it next.
    </doc>
    //
    self->dirty = TRUE;
    amq_queue_list_push (self->vhost->queue_list, self);
</method>

<method name = "selftest" inherit = "none"/>

</class>
