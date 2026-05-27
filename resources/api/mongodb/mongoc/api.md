# MongoDB C Driver Module API (`mongoc`)

## 执行环境

| 属性 | 值 |
|------|-----|
| **调用线程** | 调用者线程（通常为主线程）。所有 mongoc 函数为同步调用，在调用线程上执行。libmongoc C driver 内部使用线程池进行后台 I/O（连接管理、心跳、服务器发现），对 Lua 层透明。 |
| **线程安全** | 部分。`mongoc.Client` / `mongoc.ClientPool` 在 libmongoc 层面是线程安全的，`ClientPool:pop()` / `ClientPool:push()` 可在多线程使用。但其他 userdata 实例（`Database`、`Collection`、`Cursor`、`Session` 等）不应跨线程共享——它们的 C 对象非线程安全。 |
| **回调线程** | APM 回调（`ApmCallbacks`）由 libmongoc 内部线程触发（命令 started/succeeded/failed、心跳、拓扑变更等事件）。回调中操作 Lua state 需确保线程安全——通常应通过 `RunInLoop` 派发到主线程，或确保仅在持有 Lua state 的线程上操作。其他函数无回调。 |

## Overview

The `mongoc` module provides Lua bindings for the MongoDB C driver (libmongoc). All types are registered under the `mongoc` global table. These bindings work alongside the higher-level `db_service` API for asynchronous operations.

## Module

`mongoc`

---

## mongoc.Uri

**Metatable:** `"mongoc.uri"`

MongoDB connection URI parser and builder.

### Static Functions

#### `mongoc.Uri.new(uri_string)`

Creates a URI from a connection string.

| Parameter | Type | Description |
|-----------|------|-------------|
| `uri_string` | `string` | MongoDB connection string (e.g. `"mongodb://localhost:27017"`) |

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `uri:get_string()` | `string` | Full URI string |
| `uri:get_hosts()` | `userdata` | Host list |
| `uri:get_database()` | `string` | Default database name |
| `uri:get_option(name)` | `string` | Get URI option value |

---

## mongoc.Client

**Metatable:** `"mongoc.client"`

MongoDB client connection. Created from a URI.

### Static Functions

#### `mongoc.Client.new(uri)`

Creates a new client from a URI.

| Parameter | Type | Description |
|-----------|------|-------------|
| `uri` | `userdata` | `mongoc.Uri` instance |

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `client:get_database(name)` | `userdata` | Get a database handle |
| `client:get_default_database()` | `userdata` | Get the default database from URI |
| `client:get_collection(db, coll)` | `userdata` | Shortcut: get a collection handle |
| `client:set_appname(name)` | — | Set application name for server monitoring |
| `client:command(db_name, cmd_doc, [read_prefs])` | `(userdata, userdata)` | Run a database command, returns (reply, error) |
| `client:command_simple(db_name, cmd_doc)` | `userdata` | Run a simple command, returns reply |
| `client:find_databases()` | `userdata` | List databases |
| `client:find_collections(db_name)` | `userdata` | List collections in a database |
| `client:watch(pipeline, [opts])` | `userdata` | Open a change stream on the deployment |
| `client:start_session([opts])` | `userdata` | Start a client session |
| `client:enable_auto_encryption(opts)` | `bool` | Enable automatic client-side encryption |
| `client:set_server_api(api)` | `bool` | Set server API version |
| `client:destroy()` | — | Explicitly destroy the client |
| `client:ping([timeout_ms])` | `bool` | Ping the deployment |
| `client:get_server_descriptions()` | `userdata` | Get server topology descriptions |
| `client:check_is_dag()` | `bool` | Check if connected to a DAG (Diagnosable Adapter Group) |

---

## mongoc.Database

**Metatable:** `"mongoc.database"`

MongoDB database handle.

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `db:get_name()` | `string` | Database name |
| `db:get_collection(name)` | `userdata` | Get a collection handle |
| `db:has_collection(name)` | `bool` | Check if collection exists |
| `db:create_collection(name, [opts])` | `userdata` | Create a new collection |
| `db:list_collections([filter])` | `userdata` | List collections as cursor |
| `db:list_collection_names()` | `userdata` | List collection name strings |
| `db:run_command(cmd_doc, [read_prefs])` | `(userdata, userdata)` | Run a database command |
| `db:run_command_simple(cmd_doc)` | `userdata` | Run a simple command, returns reply |
| `db:watch(pipeline, [opts])` | `userdata` | Open a change stream |
| `db:aggregate(pipeline, [opts])` | `userdata` | Run an aggregation pipeline |
| `db:drop()` | `bool` | Drop the database |
| `db:gridfs([prefix])` | `userdata` | Get a GridFS handle |
| `db:gridfs_bucket([opts])` | `userdata` | Get a GridFS bucket |
| `db:destroy()` | — | Explicitly destroy the database handle |

---

## mongoc.Collection

**Metatable:** `"mongoc.collection"`

MongoDB collection handle for CRUD operations.

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `coll:get_name()` | `string` | Collection name |
| `coll:find(filter, [opts])` | `userdata` | Find documents, returns cursor |
| `coll:find_with_opts(filter, opts)` | `userdata` | Find with options document, returns cursor |
| `coll:find_one(filter, [opts])` | `userdata` or `nil` | Find a single document |
| `coll:aggregate(pipeline, [opts])` | `userdata` | Aggregate, returns cursor |
| `coll:count_documents(filter, [opts])` | `integer` | Count matching documents |
| `coll:estimated_document_count()` | `integer` | Estimated total document count |
| `coll:insert_one(doc, [opts])` | `(bool, userdata)` | Insert one document |
| `coll:insert_many(docs_array, [opts])` | `(bool, userdata)` | Insert multiple documents |
| `coll:update_one(filter, update, [opts])` | `(bool, userdata)` | Update one document |
| `coll:update_many(filter, update, [opts])` | `(bool, userdata)` | Update many documents |
| `coll:replace_one(filter, replacement, [opts])` | `(bool, userdata)` | Replace one document |
| `coll:delete_one(filter, [opts])` | `(bool, userdata)` | Delete one document |
| `coll:delete_many(filter, [opts])` | `(bool, userdata)` | Delete many documents |
| `coll:find_one_and_update(filter, update, opts)` | `userdata` or `nil` | Find one and update |
| `coll:find_one_and_replace(filter, repl, opts)` | `userdata` or `nil` | Find one and replace |
| `coll:find_one_and_delete(filter, opts)` | `userdata` or `nil` | Find one and delete |
| `coll:create_bulk_operation([opts])` | `userdata` | Create ordered/unordered bulk operation |
| `coll:bulk_write(bulk_write, [opts])` | `userdata` | Execute bulk write, returns result |
| `coll:create_index(keys_doc, opts, [commit_quorum])` | `bool` | Create an index |
| `coll:create_indexes(models_array)` | `bool` | Create multiple indexes |
| `coll:drop_index(name)` | `bool` | Drop an index by name |
| `coll:drop_indexes()` | `bool` | Drop all indexes |
| `coll:list_indexes()` | `userdata` | List indexes as cursor |
| `coll:find_indexes()` | `userdata` | Find indexes |
| `coll:watch(pipeline, [opts])` | `userdata` | Open a change stream |
| `coll:rename(new_db, new_name, [drop_target])` | `bool` | Rename collection |
| `coll:drop()` | `bool` | Drop the collection |
| `coll:stats()` | `userdata` | Collection statistics |
| `coll:destroy()` | — | Explicitly destroy the collection handle |

---

## mongoc.Cursor

**Metatable:** `"mongoc.cursor"`

Iterates query results.

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `cursor:next()` | `userdata` or `nil` | Get next BSON document |
| `cursor:to_array()` | `table` | Read all remaining documents into a Lua array |
| `cursor:error()` | `(integer, string)` | Get error code and message |
| `cursor:destroy()` | — | Explicitly destroy the cursor |

---

## mongoc.ClientPool

**Metatable:** `"mongoc.client_pool"`

Thread-safe client connection pool.

### Static Functions

#### `mongoc.ClientPool.new(uri, [opts])`

Creates a new client pool.

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `pool:pop()` | `userdata` | Borrow a client from the pool |
| `pool:push(client)` | — | Return a client to the pool |
| `pool:try_pop()` | `userdata` or `nil` | Non-blocking borrow |
| `pool:destroy()` | — | Destroy the pool |

---

## mongoc.Session

**Metatable:** `"mongoc.session"`

Client session for causally consistent operations and transactions.

### Instance Methods

| Method | Description |
|--------|-------------|
| `session:start_transaction([opts])` | Start a transaction |
| `session:commit_transaction()` | Commit the transaction |
| `session:abort_transaction()` | Abort (rollback) the transaction |
| `session:advance_cluster_time(cluster_time)` | Advance cluster time |
| `session:advance_operation_time(ts)` | Advance operation time |
| `session:get_cluster_time()` → `userdata` | Get cluster time |
| `session:get_operation_time()` → `(uint32, uint32)` | Get operation time (timestamp, increment) |
| `session:destroy()` | Destroy the session |

---

## mongoc.TransactionOpts

**Metatable:** `"mongoc.transaction_opts"`

Options for transactions.

#### `mongoc.TransactionOpts.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `opts:set_read_concern(rc)` | Set transaction read concern |
| `opts:set_write_concern(wc)` | Set transaction write concern |
| `opts:set_read_prefs(rp)` | Set transaction read preferences |
| `opts:set_max_commit_time_ms(ms)` | Set max commit time |

---

## mongoc.SessionOpts

**Metatable:** `"mongoc.session_opts"`

Options for creating sessions.

#### `mongoc.SessionOpts.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `opts:set_causal_consistency(b)` | Enable/disable causal consistency |
| `opts:set_default_transaction_opts(opts)` | Set default transaction options |

---

## mongoc.ChangeStream

**Metatable:** `"mongoc.change_stream"`

Watch for changes on a collection, database, or deployment.

### Instance Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `cs:next()` | `userdata` or `nil` | Get next change event |
| `cs:error()` | `(integer, string)` | Get error details |
| `cs:destroy()` | — | Destroy the change stream |

---

## mongoc.ReadPrefs

**Metatable:** `"mongoc.read_prefs"`

Read preference configuration.

#### `mongoc.ReadPrefs.new([mode])` → `userdata`

| Method | Description |
|--------|-------------|
| `rp:set_mode(mode)` | Set read mode (primary, primaryPreferred, secondary, secondaryPreferred, nearest) |
| `rp:get_mode()` → `string` | Get current read mode |
| `rp:add_tag(tag_doc)` | Add a tag set |
| `rp:set_max_staleness_seconds(n)` | Set max staleness |

---

## mongoc.WriteConcern

**Metatable:** `"mongoc.write_concern"`

Write concern configuration.

#### `mongoc.WriteConcern.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `wc:set_w(w)` | Set write concern (integer or "majority") |
| `wc:set_wtimeout(ms)` | Set write timeout in ms |
| `wc:set_journal(b)` | Enable/disable journal |

---

## mongoc.ReadConcern

**Metatable:** `"mongoc.read_concern"`

Read concern configuration.

#### `mongoc.ReadConcern.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `rc:set_level(level)` | Set level ("local", "majority", "linearizable", "available", "snapshot") |

---

## mongoc.BulkOperation

**Metatable:** `"mongoc.bulk"`

Builds an ordered or unordered bulk write.

### Instance Methods

| Method | Description |
|--------|-------------|
| `bulk:insert(doc)` | Queue an insert |
| `bulk:update_one(filter, update, [opts])` | Queue update one |
| `bulk:update_many(filter, update, [opts])` | Queue update many |
| `bulk:replace_one(filter, replacement, [opts])` | Queue replace one |
| `bulk:delete_one(filter, [opts])` | Queue delete one |
| `bulk:delete_many(filter, [opts])` | Queue delete many |
| `bulk:execute()` → `userdata` | Execute and return result |
| `bulk:destroy()` | Destroy the bulk operation |

---

## mongoc.BulkWrite

**Metatable:** `"mongoc.bulkwrite"`

High-level bulk write execution.

#### `mongoc.BulkWrite.new(collection, [opts])` → `userdata`

| Method | Description |
|--------|-------------|
| `bw:insert(doc, [opts])` | Queue insert with per-op options |
| `bw:update_one(filter, update, [opts])` | Queue update one with per-op options |
| `bw:update_many(filter, update, [opts])` | Queue update many with per-op options |
| `bw:replace_one(filter, replacement, [opts])` | Queue replace one with per-op options |
| `bw:delete_one(filter, [opts])` | Queue delete one with per-op options |
| `bw:delete_many(filter, [opts])` | Queue delete many with per-op options |
| `bw:execute()` → `userdata` | Execute and return `BulkWriteResult` |
| `bw:destroy()` | Destroy |

---

## Per-Operation Bulk Write Opts

Each operation type has its own options type for setting per-operation write concern and collation:

| Type | Metatable |
|------|-----------|
| `mongoc.BulkWriteOpts` (global) | `"mongoc.bulk_write_opts"` |
| `mongoc.BulkWriteInsertOneOpts` | `"mongoc.bulk_write_insert_one_opts"` |
| `mongoc.BulkWriteUpdateOneOpts` | `"mongoc.bulk_write_update_one_opts"` |
| `mongoc.BulkWriteUpdateManyOpts` | `"mongoc.bulk_write_update_many_opts"` |
| `mongoc.BulkWriteReplaceOneOpts` | `"mongoc.bulk_write_replace_one_opts"` |
| `mongoc.BulkWriteDeleteOneOpts` | `"mongoc.bulk_write_delete_one_opts"` |
| `mongoc.BulkWriteDeleteManyOpts` | `"mongoc.bulk_write_delete_many_opts"` |

Each created via `.new()` and provides setters for collation, write concern, hint, upsert, array filters, etc.

---

## mongoc.BulkWriteResult

**Metatable:** `"mongoc.bulk_write_result"`

Result of a bulk write operation.

| Method | Returns | Description |
|--------|---------|-------------|
| `r:inserted_count()` | `integer` | Number of documents inserted |
| `r:matched_count()` | `integer` | Number of documents matched |
| `r:modified_count()` | `integer` | Number of documents modified |
| `r:deleted_count()` | `integer` | Number of documents deleted |
| `r:upserted_count()` | `integer` | Number of upserted documents |
| `r:upserted_ids()` | `table` | Array of upserted document IDs |

---

## mongoc.BulkWriteException

**Metatable:** `"mongoc.bulk_write_exception"`

Exception from a failed bulk write.

| Method | Returns | Description |
|--------|---------|-------------|
| `e:write_errors()` | `table` | Write error details |
| `e:write_concern_errors()` | `table` | Write concern error details |

---

## mongoc.ServerApi

**Metatable:** `"mongoc.server_api"`

Server API version declaration.

#### `mongoc.ServerApi.new(version)` → `userdata`

| Method | Description |
|--------|-------------|
| `api:set_strict(b)` | Enable/disable strict mode |
| `api:set_deprecation_errors(b)` | Enable/disable deprecation errors |

---

## mongoc.FindAndModifyOpts

**Metatable:** `"mongoc.find_and_modify_opts"`

Options for find-and-modify operations.

#### `mongoc.FindAndModifyOpts.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `opts:set_sort(doc)` | Set sort order |
| `opts:set_update(doc)` | Set update document |
| `opts:set_flags(flags)` | Set operation flags (return_new, remove, upsert) |
| `opts:set_bypass_document_validation(b)` | Bypass document validation |
| `opts:set_max_time_ms(ms)` | Set max time |
| `opts:set_collation(doc)` | Set collation |
| `opts:set_array_filters(array)` | Set array filters |
| `opts:set_hint(hint_doc)` | Set index hint |
| `opts:append(field, value)` | Append an arbitrary option |

---

## mongoc.Error

**Metatable:** `"mongoc.error"`

MongoDB error representation.

| Method | Returns | Description |
|--------|---------|-------------|
| `err:code()` | `integer` | Error code |
| `err:domain()` | `integer` | Error domain |
| `err:message()` | `string` | Error message |

---

## mongoc.HostList

**Metatable:** `"mongoc.host_list"`

List of MongoDB hosts.

#### `mongoc.HostList.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `hl:add(host, port)` | Add a host entry |
| `hl:count()` → `integer` | Number of hosts |
| `hl:get(index)` → `(string, integer)` | Get host and port by index |

---

## mongoc.SslOpts

**Metatable:** `"mongoc.ssl_opts"`

TLS/SSL configuration.

#### `mongoc.SslOpts.new()` → `userdata`

| Method | Description |
|--------|-------------|
| `opts:set_ca_file(path)` | Set CA certificate file |
| `opts:set_cert_file(path)` | Set client certificate file |
| `opts:set_key_file(path)` | Set private key file |
| `opts:set_allow_invalid_hostname(b)` | Allow invalid hostnames |

---

## GridFS

GridFS provides file storage for documents exceeding the 16MB BSON limit.

### mongoc.GridFsFileOpts

**Metatable:** `"mongoc.gridfs_file_opts"`

#### `mongoc.GridFsFileOpts.new()` → `userdata`

Options for GridFS file operations (chunk size, metadata, content type, aliases).

### mongoc.GridFsFile

**Metatable:** `"mongoc.gridfs_file"`

A GridFS file handle for reading/writing.

| Method | Description |
|--------|-------------|
| `f:read(n)` → `string` | Read up to n bytes |
| `f:write(data)` → `integer` | Write data, returns bytes written |
| `f:seek(offset, [whence])` | Seek to position |
| `f:tell()` → `integer` | Current position |
| `f:length()` → `integer` | File length |
| `f:save()` | Save and close the file |
| `f:destroy()` | Destroy without saving |

### mongoc.GridFsFileList

**Metatable:** `"mongoc.gridfs_file_list"`

List of GridFS files from a query.

| Method | Description |
|--------|-------------|
| `fl:next()` → `userdata` or `nil` | Get next GridFS file |
| `fl:destroy()` | Destroy the list |

### mongoc.GridFs

**Metatable:** `"mongoc.gridfs"`

GridFS operations on a database.

#### Created via `database:gridfs([prefix])`

| Method | Description |
|--------|-------------|
| `gfs:find(filter)` → `userdata` | Find files, returns GridFsFileList |
| `gfs:find_by_filename(name)` → `userdata` | Find files by name |
| `gfs:create_file([opts])` → `userdata` | Create a new writeable file |
| `gfs:create_file_from_stream(stream, [opts])` | Upload from a stream |
| `gfs:remove(filter)` | Remove files matching filter |
| `gfs:remove_by_filename(name)` | Remove files by name |
| `gfs:drop()` | Drop the GridFS collections |
| `gfs:destroy()` | Destroy the GridFS handle |

### mongoc.GridFsBucket

**Metatable:** `"mongoc.gridfs_bucket"`

Stream-oriented GridFS API (MongoDB 3.6+).

#### Created via `database:gridfs_bucket([opts])`

| Method | Description |
|--------|-------------|
| `b:open_upload_stream(filename, [opts])` → `userdata` | Open for writing |
| `b:open_download_stream(id)` → `userdata` | Open for reading by ID |
| `b:find(filter)` | Find files |
| `b:rename(id, new_name)` | Rename a file |
| `b:delete(id)` | Delete a file |
| `b:drop()` | Drop the bucket |
| `b:destroy()` | Destroy the bucket |

---

## mongoc.IndexModel

**Metatable:** `"mongoc.index_model"`

Index specification for `create_indexes`.

#### `mongoc.IndexModel.new(keys_doc)` → `userdata`

| Method | Description |
|--------|-------------|
| `im:set_options(opts_doc)` | Set index options (name, unique, sparse, etc.) |

---

## mongoc.ServerDescription

**Metatable:** `"mongoc.server_description"`

Description of a server in the topology.

### Instance Methods (read-only accessors)

| Method | Returns | Description |
|--------|---------|-------------|
| `sd:host()` | `string` | Server hostname |
| `sd:port()` | `integer` | Server port |
| `sd:type()` | `string` | Server type |
| `sd:round_trip_time()` | `integer` | Last RTT in ms |

---

## mongoc.TopologyDescription

**Metatable:** `"mongoc.topology_description"`

Description of the cluster topology.

| Method | Returns | Description |
|--------|---------|-------------|
| `td:type()` | `string` | Topology type (single, replicaset, sharded, load_balanced) |
| `td:has_readable_server(rp)` | `bool` | Check for readable servers |
| `td:has_writable_server()` | `bool` | Check for writable servers |
| `td:get_servers()` | `userdata` | Get server descriptions |

---

## mongoc.Stream

**Metatable:** `"mongoc.stream"`

Stream abstraction for custom I/O.

---

## mongoc.Socket

**Metatable:** `"mongoc.socket"`

Socket abstraction.

---

## Structured Logging

### mongoc.StructuredLogOpts

**Metatable:** `"mongoc.structured_log_opts"`

#### `mongoc.StructuredLogOpts.new()` → `userdata`

Configuration for structured logging output.

### mongoc.StructuredLogEntry

**Metatable:** `"mongoc.structured_log_entry"`

A single structured log entry.

---

## OIDC Authentication

### mongoc.OidcCredential

**Metatable:** `"mongoc.oidc_credential"`

OIDC credential holder.

### mongoc.OidcCallbackParams

**Metatable:** `"mongoc.oidc_callback_params"`

Parameters passed to OIDC callback.

### mongoc.OidcCallback

**Metatable:** `"mongoc.oidc_callback"`

OIDC callback function wrapper.

---

## Client-Side Field Level Encryption (CSFLE)

### mongoc.AutoEncryptionOpts

**Metatable:** `"mongoc.auto_encryption_opts"`

#### `mongoc.AutoEncryptionOpts.new()` → `userdata`

Auto-encryption configuration for `mongoc.Client`.

### mongoc.ClientEncryptionOpts

**Metatable:** `"mongoc.client_encryption_opts"`

#### `mongoc.ClientEncryptionOpts.new()` → `userdata`

Encryption options for `mongoc.ClientEncryption`.

### mongoc.ClientEncryptionEncryptOpts

**Metatable:** `"mongoc.client_encryption_encrypt_opts"`

General encrypt options.

### mongoc.ClientEncryptionEncryptRangeOpts

**Metatable:** `"mongoc.client_encryption_encrypt_range_opts"`

Range query encryption options.

### mongoc.ClientEncryptionEncryptTextPrefixOpts

**Metatable:** `"mongoc.client_encryption_encrypt_text_prefix_opts"`

Text prefix encryption options.

### mongoc.ClientEncryptionEncryptTextSuffixOpts

**Metatable:** `"mongoc.client_encryption_encrypt_text_suffix_opts"`

Text suffix encryption options.

### mongoc.ClientEncryptionEncryptTextSubstringOpts

**Metatable:** `"mongoc.client_encryption_encrypt_text_substring_opts"`

Text substring encryption options.

### mongoc.ClientEncryptionEncryptTextOpts

**Metatable:** `"mongoc.client_encryption_encrypt_text_opts"`

Text encryption options (aggregate of prefix/suffix/substring).

### mongoc.ClientEncryptionDatakeyOpts

**Metatable:** `"mongoc.client_encryption_datakey_opts"`

Data key creation options.

### mongoc.ClientEncryptionRewrapManyDatakeyResult

**Metatable:** `"mongoc.client_encryption_rewrap_many_datakey_result"`

Result of rewrap many datakeys operation.

### mongoc.ClientEncryption

**Metatable:** `"mongoc.client_encryption"`

#### `mongoc.ClientEncryption.new(client, opts)` → `userdata`

Explicit encryption/decryption operations.

| Method | Description |
|--------|-------------|
| `ce:create_datakey(kms_provider, [opts])` → `userdata` | Create a new data key, returns key Oid |
| `ce:encrypt(value, opts)` → `userdata` | Encrypt a value |
| `ce:decrypt(encrypted_value)` → `userdata` | Decrypt a value |
| `ce:rewrap_many_datakey(filter, [opts])` → `userdata` | Rewrap matching data keys |
| `ce:destroy()` | Destroy |

---

## Application Performance Monitoring (APM)

APM callbacks provide visibility into driver-server communication.

### APM Event Types

| Type | Metatable | Description |
|------|-----------|-------------|
| `mongoc.ApmCommandStartedEvent` | `"mongoc.apm_command_started_event"` | Command started |
| `mongoc.ApmCommandSucceededEvent` | `"mongoc.apm_command_succeeded_event"` | Command succeeded |
| `mongoc.ApmCommandFailedEvent` | `"mongoc.apm_command_failed_event"` | Command failed |
| `mongoc.ApmServerChangedEvent` | `"mongoc.apm_server_changed_event"` | Server description changed |
| `mongoc.ApmServerOpeningEvent` | `"mongoc.apm_server_opening_event"` | New server discovered |
| `mongoc.ApmServerClosedEvent` | `"mongoc.apm_server_closed_event"` | Server removed |
| `mongoc.ApmTopologyChangedEvent` | `"mongoc.apm_topology_changed_event"` | Topology changed |
| `mongoc.ApmTopologyOpeningEvent` | `"mongoc.apm_topology_opening_event"` | Topology initializing |
| `mongoc.ApmTopologyClosedEvent` | `"mongoc.apm_topology_closed_event"` | Topology closed |
| `mongoc.ApmServerHeartbeatStartedEvent` | `"mongoc.apm_server_heartbeat_started_event"` | Heartbeat started |
| `mongoc.ApmServerHeartbeatSucceededEvent` | `"mongoc.apm_server_heartbeat_succeeded_event"` | Heartbeat succeeded |
| `mongoc.ApmServerHeartbeatFailedEvent` | `"mongoc.apm_server_heartbeat_failed_event"` | Heartbeat failed |

Each event type has read-only accessors for driver metadata (command name, database, request_id, duration, server host, topology ID, etc.).

### mongoc.ApmCallbacks

**Metatable:** `"mongoc.apm_callbacks"`

#### `mongoc.ApmCallbacks.new()` → `userdata`

Register event handlers:

```
callbacks:set_command_started(function(event) ... end)
callbacks:set_command_succeeded(function(event) ... end)
callbacks:set_command_failed(function(event) ... end)
callbacks:set_server_changed(function(event) ... end)
callbacks:set_server_opening(function(event) ... end)
callbacks:set_server_closed(function(event) ... end)
callbacks:set_topology_changed(function(event) ... end)
callbacks:set_topology_opening(function(event) ... end)
callbacks:set_topology_closed(function(event) ... end)
callbacks:set_server_heartbeat_started(function(event) ... end)
callbacks:set_server_heartbeat_succeeded(function(event) ... end)
callbacks:set_server_heartbeat_failed(function(event) ... end)
```

---

## mongoc.Optional

**Metatable:** `"mongoc.optional"`

Generic optional value wrapper for MongoDB driver APIs that accept optional parameters.

---

## mongoc.Misc

Miscellaneous utility functions under `mongoc`:

| Function | Description |
|----------|-------------|
| `mongoc.init()` | Initialize libmongoc (called once automatically) |
| `mongoc.cleanup()` | Cleanup libmongoc |
| `mongoc.get_version()` → `string` | Get libmongoc version |
| `mongoc.get_major_version()` → `integer` | Get major version number |
| `mongoc.get_minor_version()` → `integer` | Get minor version number |

---

## 类型详述

mongoc 模块中所有对象均为 Lua full userdata，内部持有 libmongoc C 结构体指针。下表列出常用参数/返回值与底层 C 类型的对应关系：

| 参数/返回值 | Lua 类型 | 底层 C 类型 | 获取/推送方式 |
|-----------|----------|------------|-------------|
| Uri / Client / Database / Collection | `userdata` | `mongoc_uri_t*` / `mongoc_client_t*` / `mongoc_database_t*` / `mongoc_collection_t*` | `lua_newuserdata` + metatable, `__gc` 调用 `_destroy()` |
| Cursor / ChangeStream | `userdata` | `mongoc_cursor_t*` / `mongoc_change_stream_t*` | `lua_newuserdata` + metatable |
| ClientPool / Session | `userdata` | `mongoc_client_pool_t*` / `mongoc_client_session_t*` | `lua_newuserdata` + metatable |
| ReadPrefs / WriteConcern / ReadConcern | `userdata` | `mongoc_read_prefs_t*` / `mongoc_write_concern_t*` / `mongoc_read_concern_t*` | `lua_newuserdata` + metatable |
| BulkOperation / BulkWrite / BulkWriteResult | `userdata` | `mongoc_bulk_operation_t*` / `mongoc_bulk_write_t*` / `mongoc_bulk_write_result_t*` | `lua_newuserdata` + metatable |
| GridFsFile / GridFs / GridFsBucket | `userdata` | `mongoc_gridfs_file_t*` / `mongoc_gridfs_t*` / `gridfs_bucket_t*` | `lua_newuserdata` + metatable |
| ClientEncryption | `userdata` | `mongoc_client_encryption_t*` | `lua_newuserdata` + metatable |
| 所有 Opts 类型 (TransactionOpts, SessionOpts, 各种 EncryptOpts 等) | `userdata` | 对应 `mongoc_*_t*` 或自定义结构体 | `lua_newuserdata` + metatable |
| ApmCallbacks / APM Event 类型 | `userdata` | `mongoc_apm_callbacks_t*` / `mongoc_apm_*_t*` | `lua_newuserdata` + metatable |
| 所有 `new(uri_string)` 的 uri_string | `string` | `const char*` | `luaL_checkstring` |
| 所有 `new(doc/uri/etc.)` 的构造参数 | `userdata` | 对应 userdata 类型 | metatable 检查 (`luaL_checkudata` 或 `CheckUserdata`) |
| filter / pipeline / cmd_doc / keys_doc 等 BSON 参数 | `userdata` | `bson_t*` (Document userdata) | 从 userdata 提取 `bson_t*` 指针 |
| `cursor:next()` 返回值 | `userdata` or `nil` | `const bson_t*` → full userdata 拷贝 | 拷贝 bson 数据到新 userdata |
| `coll:insert_one/update_one/...` 返回值 (ok) | `boolean` | `bool` | `lua_pushboolean` |
| `coll:insert_one/update_one/...` 返回值 (err) | `userdata` or `nil` | `bson_error_t*` → full userdata | `lua_newuserdata` + `"mongoc.error"` metatable |
| `coll:count_documents()` 返回值 | `integer` | `int64_t` | `lua_pushinteger` |
| `client:ping()` 返回值 | `boolean` | `bool` | `lua_pushboolean` |
| `get_version()` 返回值 | `string` | `const char*` (mongoc_get_version) | `lua_pushstring` |

## Example

```lua
-- Create a client
local uri = mongoc.Uri.new("mongodb://localhost:27017")
local client = mongoc.Client.new(uri)

-- Get a database and collection
local db = client:get_database("game_db")
local coll = db:get_collection("players")

-- Insert a document
local doc = bson.Document.new_from_json('{"name":"player1","score":100}')
local ok, err = coll:insert_one(doc)

-- Find documents
local filter = bson.Document.new_from_json('{"score":{"$gt":50}}')
local cursor = coll:find(filter)
while true do
    local d = cursor:next()
    if not d then break end
    log_info("Found: " .. d:to_json())
end

-- Bulk write
local bw = mongoc.BulkWrite.new(coll)
local d1 = bson.Document.new_from_json('{"name":"a"}')
local d2 = bson.Document.new_from_json('{"name":"b"}')
bw:insert(d1)
bw:insert(d2)
local result = bw:execute()
log_info("Inserted " .. result:inserted_count() .. " documents")

-- Cleanup
coll:destroy()
db:destroy()
client:destroy()
```
