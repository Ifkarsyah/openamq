<?xml version="1.0"?>
<protocol>

<class
    name    = "cluster"
    handler = "cluster"
    index   = "12"
  >
  methods for intracluster communications

<doc>
  The cluster methods are used by peers in a cluster.
</doc>

<doc name = "rule">
  Standard clients MUST NOT use the cluster class, which is reserved
  for AMQP servers participating in cluster communications.
</doc>

<doc name = "grammar">
    cluster             = C:PUBLISH
                        / C:ROOT
                        / C:BIND
</doc>

<chassis name = "server" implement = "MAY" />
<chassis name = "client" implement = "MAY" />

<!--
    These are the properties for a proxied method, which is wrapped as
    a content and sent using the Cluster.Proxy method.
 -->

<field name = "origin spid" type = "shortstr">
    Spid of originating peer
</field>
<field name = "method name" type = "shortstr">
    Proxied method name, for tracing and debugging
</field>
<field name = "stateful" type = "octet">
    Does method form part of cluster state?
</field>
<field name = "fanout" type = "octet">
    Re-distribute method to secondary peers?
</field>


<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->

<method name = "proxy" content = "1">
  proxies a cluster method
  <doc>
    This method proxies a cluster method, which is an AMQP method
    wrapped in cluster metadata.  Cluster methods are always handled
    by the cluster service, bypassing the normal exchange mechanism.
    In some exchanges, a proxy request can be answered by a proxy
    response.
  </doc>
  <chassis name = "server" implement = "MUST" />

  <field name = "client connection" type = "shortstr">
    original client connection
    <doc>
    The original client connection for the method, encoded by the
    sending peer.
    </doc>
  </field>

  <field name = "client channel" type = "short">
    original client channel
    <doc>
    The original client channel for the method, as defined by the
    sending peer.
    </doc>
  </field>
</method>


<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->

<method name = "root">
enable or disable server as root
  <doc>
    This method tells the cluster whether the specified server is
    taking or dropping the 'root' role.  For a full explanation of
    the AMQ cluster design please refer to the AMQ Cluster design
    documentation and specifications.
  </doc>
  <chassis name = "server" implement = "MUST" />
  <chassis name = "client" implement = "MUST" />

  <field name = "active" type = "bit">
     activate or disactivate root
    <doc>
      Specifies whether the originating server is taking or dropping
      the root role.
    </doc>
  </field>
</method>

<!-- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -->

<method name = "bind">
  bind local exchange to remote exchange
  <doc>
    This method binds an exchange on one server to an exchange on
    another server.
  </doc>
  <chassis name = "server" implement = "MUST" />
  <chassis name = "client" implement = "MUST" />

  <field name = "exchange" domain = "exchange name">
    <doc>
      The name of the remote exchange to bind to. The exchange must
      exist both on the originating server, and the target server.
      The same exchange name is used when publishing messages back
      to the originating server.
    </doc>
    <doc name = "rule">
      If the exchange does not exist the server MUST raise a channel
      exception with reply code 404 (not found).
    </doc>
  </field>

  <field name = "routing key" type = "shortstr">
     message routing key
    <doc>
      Specifies the routing key for the binding.  The routing key is
      used for routing messages depending on the exchange configuration.
      Not all exchanges use a routing key - refer to the specific exchange
      documentation.
    </doc>
  </field>

  <field name = "arguments" type = "table">
    arguments for binding
    <doc>
      A set of arguments for the binding.  The syntax and semantics of
      these arguments depends on the exchange class.
    </doc>
  </field>
</method>

</class>
</protocol>
