<?xml?>
<class
    name    = "amq_exchange_headers"
    comment = "Headers exchange class"
    version = "1.0"
    script  = "icl_gen"
    >
<doc>
This class implements the headers exchange, which routes messages
based on the message header fields (the field table called "headers"
that is in every content header).  Max. unique fields exchange is
limited by size of amq_index_hash table.
</doc>

<inherit class = "amq_exchange_base" />

<context>
    amq_index_hash_t
        *index_hash;                    //  Access by text key
    amq_index_array_t
        *index_array;                   //  Access by number 0..n-1
</context>

<method name = "new">
    self->index_hash  = amq_index_hash_new  ();
    self->index_array = amq_index_array_new ();
</method>

<method name = "destroy">
    amq_index_hash_destroy  (&self->index_hash);
    amq_index_array_destroy (&self->index_array);
</method>

<method name = "compile">
    <doc>
    Compiles a headers binding. The arguments hold a set of fields
    to match on.  Each field with a non-empty value is matched for
    that field/value pair.  Each field with an empty value is matched
    for presence only (matching any value in the message).  Fields
    prefixed by X- are not matched on.  The field called "X-match"
    can have the value "all" or "any", meaning logical AND or OR of
    all matches.  It defaults to "all".
    </doc>
    <local>
    asl_field_list_t
        *fields;                        //  Decoded arguments
    asl_field_t
        *field;                         //  Field on which to match
    icl_shortstr_t
        index_key;                      //  Index key to match on
    </local>
    //
#   define FIELD_NAME_MAX   30          //  Arbitrary limit to get
#   define FIELD_VALUE_MAX  30          //    name=value into shortstr
#   define MATCH_ALL_KEY    "*"         //  Match all messages

    fields = asl_field_list_new (binding->arguments);
    if (fields) {
        binding->match_all = TRUE;      //  By default, want full match
        field = asl_field_list_first (fields);
        while (field) {
            if (field->name [0] == 'X' && field->name [1] == '-') {
                if (streq (field->name, "X-match")) {
                    if (streq (asl_field_string (field), "any")) {
                        if (amq_server_config_debug_route (amq_server_config))
                            smt_log_print (amq_broker->debug_log,
                                "X: select   %s: match=any", self->exchange->name, index_key);
                        binding->match_all = FALSE;
                    }
                }
                else
                    smt_log_print (amq_broker->alert_log,
                        "W: unknown field '%s' in bind arguments", field->name);
            }
            else {
                //  Index key is field name, or field name and value
                //  We truncate name and value to sensible limits

                icl_shortstr_ncpy (index_key, field->name, FIELD_NAME_MAX);
                if (field->type != 'V') {
                    icl_shortstr_cat  (index_key, "=");
                    icl_shortstr_ncat (index_key,
                        asl_field_string (field), FIELD_VALUE_MAX);
                }
                rc = s_compile_binding (self, binding, index_key);
            }
            field = asl_field_list_next (&field);
        }
        asl_field_list_destroy (&fields);

        //  If zero fields specified, match on all messages
        if (binding->field_count == 0)
            rc = s_compile_binding (self, binding, MATCH_ALL_KEY);

        if (rc)
            amq_server_connection_error (
                channel? channel->connection: NULL,
                ASL_COMMAND_INVALID,
                "Queue binding failed, too many bindings");
    }
    else {
        rc = 1;
        amq_server_connection_error (
            channel? channel->connection: NULL,
            ASL_COMMAND_INVALID,
            "Invalid binding arguments");
    }
</method>

<method name = "publish">
    <local>
    asl_field_list_t
        *headers = NULL;                //  Decoded header fields
    icl_shortstr_t
        index_key;                      //  Index key to match on
    amq_hitset_t
        *hitset;                        //  Match hitset
    asl_field_t
        *field;                         //  Field on which to match
    amq_binding_t
        *binding;                       //  Binding object
    int
        binding_nbr;                    //  Binding number, 1..n from bindset
    </local>
    //
    if (method->class_id == AMQ_SERVER_BASIC)
        headers = asl_field_list_new (basic_content->headers);

    if (headers) {
        hitset = amq_hitset_new ();
        field = asl_field_list_first (headers);
        while (field) {
            //  Match on field presence
            icl_shortstr_ncpy (index_key, field->name, FIELD_NAME_MAX);
            amq_hitset_collect (hitset, self->index_hash, index_key);

            //  Match on field value
            icl_shortstr_cat  (index_key, "=");
            icl_shortstr_ncat (index_key, asl_field_string (field), FIELD_VALUE_MAX);
            amq_hitset_collect (hitset, self->index_hash, index_key);

            field = asl_field_list_next (&field);
        }
        //  Add "match all" bindings
        amq_hitset_collect (hitset, self->index_hash, MATCH_ALL_KEY);

        //  The hitset now represents all matching bindings
        for (binding_nbr = hitset->lowest; binding_nbr <= hitset->highest; binding_nbr++) {
            binding = self->exchange->binding_index->data [binding_nbr];
            if (binding) {
                if ((binding->match_all && hitset->hit_count [binding_nbr] == binding->field_count)
                || (!binding->match_all && hitset->hit_count [binding_nbr] > 0)) {
                    delivered += amq_binding_publish (binding, channel, method);
                    if (amq_server_config_debug_route (amq_server_config))
                        smt_log_print (amq_broker->debug_log,
                            "X: have_hit %s: match=%s hits=%d binding=%d",
                            self->exchange->name,
                            binding->match_all? "all": "any",
                            hitset->hit_count [binding_nbr], binding_nbr);
                }
            }
        }
        amq_hitset_destroy (&hitset);
        asl_field_list_destroy (&headers);
    }
</method>

<private name = "header">
static int
    s_compile_binding ($(selftype) *self, amq_binding_t *binding, char *index_key);
</private>

<private>
static int
s_compile_binding (
    $(selftype)   *self,
    amq_binding_t *binding,
    char          *index_key)
{
    int
        rc = 0;                         //  Return code
    amq_index_t
        *index;                         //  Index reference from index_hash

    if (amq_server_config_debug_route (amq_server_config))
        smt_log_print (amq_broker->debug_log,
            "X: index    %s: request=%s binding=%d",
                self->exchange->name, index_key, binding->index);

    if (binding->index < IPR_BITS_SIZE_BITS) {
        index = amq_index_hash_search (self->index_hash, index_key);
        if (index == NULL)
            index = amq_index_new (self->index_hash, index_key, self->index_array);

        //  Cross-reference binding and index
        ipr_bits_set (index->bindset, binding->index);
        ipr_looseref_queue (binding->index_list, index);

        //  We count the number of fields to allow AND matching
        binding->field_count++;
        amq_index_unlink (&index);
    }
    else {
        smt_log_print (amq_broker->alert_log,
            "E: too many bindings, limit is %d", IPR_BITS_SIZE_BITS);
        rc = 1;
    }
    return (rc);
}
</private>
</class>