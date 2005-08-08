<?xml?>
<class
    name      = "amq_binding"
    comment   = "An exchange binding"
    version   = "1.0"
    copyright = "Copyright (c) 2004-2005 iMatix Corporation"
    script    = "icl_gen"
    >
<doc>
This class implements an exchange binding, which is a collection of
queues and other exchanges that share the same binding arguments.
This class runs lock-free as a child of the asynchronous exchange
class.
</doc>

<inherit class = "icl_object">
    <option name = "alloc" value = "cache" />
</inherit>
<inherit class = "ipr_list_item">
    <option name = "prefix" value = "list" />
    <option name = "rwlock" value = "0" />
</inherit>
<import class = "amq_server_classes" />

<context>
    ipr_looseref_list_t
        *queue_list;                    //  List of queues for binding
    ipr_looseref_list_t
        *exchange_list;                 //  List of exchanges for binding
    ipr_longstr_t
        *arguments;                     //  Binding arguments
</context>

<method name = "new">
    <argument name = "arguments" type = "ipr_longstr_t *">Arguments</argument>
    self->queue_list    = ipr_looseref_list_new ();
    self->exchange_list = ipr_looseref_list_new ();
    self->arguments     = ipr_longstr_dup (arguments);
</method>

<method name = "destroy">
    <local>
    amq_queue_t
        *queue;                         //  Queue object reference
    amq_exchange_t
        *exchange;                      //  Exchange object reference
    </local>
    //
    //  Drop all references to queues and exchanges for the binding
    while ((queue = (amq_queue_t *) ipr_looseref_pop (self->queue_list)))
        amq_queue_unlink (&queue);
    while ((exchange = (amq_exchange_t *) ipr_looseref_pop (self->exchange_list)))
        amq_exchange_unlink (&exchange);

    ipr_looseref_list_destroy (&self->queue_list);
    ipr_looseref_list_destroy (&self->exchange_list);
    ipr_longstr_destroy       (&self->arguments);
</method>

<method name = "bind queue" template = "function">
    <doc>
    Attach queue to current binding.
    </doc>
    <argument name = "queue" type = "amq_queue_t *">Queue to bind</argument>
    //
    amq_queue_link (queue);
    ipr_looseref_queue (self->queue_list, queue);
</method>

<method name = "bind exchange" template = "function">
    <doc>
    Attach exchange to current binding.
    </doc>
    <argument name = "exchange" type = "amq_exchange_t *">Exchange to bind</argument>
    //
    amq_exchange_link (exchange);
    ipr_looseref_queue (self->exchange_list, exchange);
</method>

<method name = "publish" template = "function">
    <doc>
    Publish message to all queues and exchanges defined for the
    binding.  Returns number of active consumers found on all queues
    published to.
    </doc>
    <argument name = "channel"   type = "amq_server_channel_t *">Channel for reply</argument>
    <argument name = "class id"  type = "int">The content class</argument>
    <argument name = "content"   type = "void *">The message content</argument>
    <argument name = "mandatory" type = "Bool">Warn if unroutable</argument>
    <argument name = "immediate" type = "Bool">Warn if no consumers</argument>
    <local>
    ipr_looseref_t
        *looseref;                      //  Bound object
    amq_queue_t
        *queue;                         //  Queue we publish to
    amq_exchange_t
        *exchange;                      //  Exchange we publish to
    </local>
    //
    //  Publish to all queues, sending method to async queue class
    looseref = ipr_looseref_list_first (self->queue_list);
    while (looseref) {
        queue = (amq_queue_t *) (looseref->object);
        amq_queue_publish (queue, channel, class_id, content, immediate);
        looseref = ipr_looseref_list_next (&looseref);
    }
    //  Publish to all exchanges, sending method to async exchange class
    looseref = ipr_looseref_list_first (self->exchange_list);
    while (looseref) {
        exchange = (amq_exchange_t *) (looseref->object);
        amq_exchange_publish (exchange, channel, class_id, content, mandatory, immediate);
        looseref = ipr_looseref_list_next (&looseref);
    }
</method>

<method name = "selftest" />

</class>
