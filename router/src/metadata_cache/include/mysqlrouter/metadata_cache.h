/*
  Copyright (c) 2016, 2021, Oracle and/or its affiliates.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef MYSQLROUTER_METADATA_CACHE_INCLUDED
#define MYSQLROUTER_METADATA_CACHE_INCLUDED

#include <atomic>
#include <exception>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "my_rapidjson_size_t.h"

#include <rapidjson/document.h>

#include "mysql/harness/stdx/monitor.h"
#include "mysql_router_thread.h"
#include "mysqlrouter/cluster_metadata.h"
#include "mysqlrouter/datatypes.h"
#include "mysqlrouter/utils.h"
#include "tcp_address.h"

#ifdef _WIN32
#ifdef metadata_cache_DEFINE_STATIC
#define METADATA_API
#else
#ifdef metadata_cache_EXPORTS
#define METADATA_API __declspec(dllexport)
#else
#define METADATA_API __declspec(dllimport)
#endif
#endif
#else
#define METADATA_API
#endif

namespace metadata_cache {

extern const uint16_t kDefaultMetadataPort;
extern const std::string kDefaultMetadataAddress;
extern const std::string kDefaultMetadataUser;
extern const std::string kDefaultMetadataPassword;
extern const std::chrono::milliseconds kDefaultMetadataTTL;
extern const std::chrono::milliseconds kDefaultAuthCacheTTL;
extern const std::chrono::milliseconds kDefaultAuthCacheRefreshInterval;
extern const std::string kDefaultMetadataCluster;
extern const unsigned int kDefaultConnectTimeout;
extern const unsigned int kDefaultReadTimeout;

extern const std::string kNodeTagHidden;
extern const std::string kNodeTagDisconnectWhenHidden;
extern const bool kNodeTagHiddenDefault;
extern const bool kNodeTagDisconnectWhenHiddenDefault;

enum class ReplicasetStatus {
  AvailableWritable,
  AvailableReadOnly,
  UnavailableRecovering,
  Unavailable
};

enum class ServerMode { ReadWrite, ReadOnly, Unavailable };

enum class InstanceStatus {
  Reachable,
  InvalidHost,  // Network connection cannot even be attempted (ie bad IP)
  Unreachable,  // TCP connection cannot be opened
  Unusable      // TCP connection can be opened but session can't be opened
};

/** @class ManagedInstance
 *
 * Class ManagedInstance represents a server managed by the topology.
 */
class METADATA_API ManagedInstance {
 public:
  ManagedInstance() = default;
  ManagedInstance(const std::string &p_replicaset_name,
                  const std::string &p_mysql_server_uuid,
                  const ServerMode p_mode, const std::string &p_host,
                  const uint16_t p_port, const uint16_t p_xport);

  using TCPAddress = mysql_harness::TCPAddress;
  explicit ManagedInstance(const TCPAddress &addr);
  operator TCPAddress() const;
  bool operator==(const ManagedInstance &other) const;

  /** @brief The name of the replicaset to which the server belongs */
  std::string replicaset_name;
  /** @brief The uuid of the MySQL server */
  std::string mysql_server_uuid;
  /** @brief The mode of the server */
  ServerMode mode;
  /** @brief The host name on which the server is running */
  std::string host;
  /** The port number in which the server is running */
  uint16_t port;
  /** The X protocol port number in which the server is running */
  uint16_t xport;
  /** Should the node be hidden from the application to use it */
  bool hidden{kNodeTagHiddenDefault};
  /** Should the Router disconnect existing client sessions to the node when it
   * is hidden */
  bool disconnect_existing_sessions_when_hidden{
      kNodeTagDisconnectWhenHiddenDefault};
};

/** @class ManagedReplicaSet
 * Represents a replicaset (a GR group or AR members)
 */
class METADATA_API ManagedReplicaSet {
 public:
  /** @brief The name of the replica set */
  std::string name;
  /** @brief List of the members that belong to the replicaset */
  std::vector<metadata_cache::ManagedInstance> members;
  /** @brief Whether replicaset is in single_primary_mode (from PFS in case of
   * GR) */
  bool single_primary_mode;
  /** @brief Id of the view this metadata represents (only used for AR now)*/
  unsigned view_id{0};
  /** @brief Metadata for the replicaset is not consistent (only applicable for
   * the GR cluster when the data in the GR metadata is not consistent with the
   * cluster metadata)*/
  bool md_discrepancy{false};
};

/** @class connection_error
 *
 * Class that represents all the exceptions thrown while trying to
 * connect with a node managed by the topology.
 *
 */
class connection_error : public std::runtime_error {
 public:
  explicit connection_error(const std::string &what_arg)
      : std::runtime_error(what_arg) {}
};

/** @class metadata_error
 * Class that represents all the exceptions that are thrown while fetching the
 * metadata.
 *
 */
class metadata_error : public std::runtime_error {
 public:
  explicit metadata_error(const std::string &what_arg)
      : std::runtime_error(what_arg) {}
};

/** @class LookupResult
 *
 * Class holding result after looking up data in the cache.
 */
class METADATA_API LookupResult {
 public:
  /** @brief Constructor */
  LookupResult(const std::vector<ManagedInstance> &instance_vector_)
      : instance_vector(instance_vector_) {}

  /** @brief List of ManagedInstance objects */
  const std::vector<metadata_cache::ManagedInstance> instance_vector;
};

/**
 * @brief Abstract class that provides interface for listener on
 *        replicaset status changes.
 *
 *        When state of replicaset is changed, notify function is called.
 */
class METADATA_API ReplicasetStateListenerInterface {
 public:
  /**
   * @brief Callback function that is called when state of replicaset is
   * changed.
   *
   * @param instances allowed nodes
   * @param md_servers_reachable true if metadata changed, false if metadata
   * unavailable
   * @param view_id current metadata view_id in case of ReplicaSet cluster
   */
  virtual void notify_instances_changed(const LookupResult &instances,
                                        const bool md_servers_reachable,
                                        const unsigned view_id) = 0;

  ReplicasetStateListenerInterface() = default;
  // disable copy as it isn't needed right now. Feel free to enable
  // must be explicitly defined though.
  explicit ReplicasetStateListenerInterface(
      const ReplicasetStateListenerInterface &) = delete;
  ReplicasetStateListenerInterface &operator=(
      const ReplicasetStateListenerInterface &) = delete;
  virtual ~ReplicasetStateListenerInterface();
};

/**
 * @brief Abstract class that provides interface for listener on
 *        whether the listening sockets acceptors state should be updated.
 */
class METADATA_API AcceptorUpdateHandlerInterface {
 public:
  /**
   * @brief Callback function that is called when the state of the sockets
   * acceptors is handled during the metadata refresh.
   *
   * @param instances allowed nodes for new connections
   */
  virtual bool update_socket_acceptor_state(const LookupResult &instances) = 0;

  AcceptorUpdateHandlerInterface() = default;

  AcceptorUpdateHandlerInterface(const AcceptorUpdateHandlerInterface &) =
      default;
  AcceptorUpdateHandlerInterface &operator=(
      const AcceptorUpdateHandlerInterface &) = default;

  AcceptorUpdateHandlerInterface(AcceptorUpdateHandlerInterface &&) = default;
  AcceptorUpdateHandlerInterface &operator=(AcceptorUpdateHandlerInterface &&) =
      default;

  virtual ~AcceptorUpdateHandlerInterface() = default;
};

/**
 * @brief Abstract class that provides interface for adding and removing
 *        observers on replicaset status changes.
 *
 *        When state of replicaset is changed, then
 * ReplicasetStateListenerInterface::notify_instances_changed function is called
 * for every registered observer.
 */
class METADATA_API ReplicasetStateNotifierInterface {
 public:
  /**
   * @brief Register observer that is notified when there is a change in the
   * replicaset nodes setup/state discovered.
   *
   * @param replicaset_name name of the replicaset
   * @param listener Observer object that is notified when replicaset nodes
   * state is changed.
   *
   * @throw std::runtime_error if metadata cache not initialized
   */
  virtual void add_state_listener(
      const std::string &replicaset_name,
      ReplicasetStateListenerInterface *listener) = 0;

  /**
   * @brief Unregister observer previously registered with add_state_listener()
   *
   * @param replicaset_name name of the replicaset
   * @param listener Observer object that should be unregistered.
   *
   * @throw std::runtime_error if metadata cache not initialized
   */
  virtual void remove_state_listener(
      const std::string &replicaset_name,
      ReplicasetStateListenerInterface *listener) = 0;

  ReplicasetStateNotifierInterface() = default;
  // disable copy as it isn't needed right now. Feel free to enable
  // must be explicitly defined though.
  explicit ReplicasetStateNotifierInterface(
      const ReplicasetStateNotifierInterface &) = delete;
  ReplicasetStateNotifierInterface &operator=(
      const ReplicasetStateNotifierInterface &) = delete;
  virtual ~ReplicasetStateNotifierInterface();
};

class METADATA_API MetadataCacheAPIBase
    : public ReplicasetStateNotifierInterface {
 public:
  /** @brief Initialize a MetadataCache object and start caching
   *
   * The metadata_cache::cache_init function will initialize a MetadataCache
   * object using the given arguments and store it globally using the given
   * cache_name.
   *
   * Parameters host, port, user, password are used to setup the connection with
   * the metadata server.
   *
   * Cache name given by cache_name can be empty, but must be unique.
   *
   * The parameters connection_timeout and connection_attempts are used when
   * connected to the metadata server.
   *
   * Throws a std::runtime_error when the cache object was already
   * initialized.
   *
   * @param cluster_type type of the cluster the metadata cache object will
   *                     represent (GR or ReplicaSet)
   * @param router_id id of the router in the cluster metadata
   * @param cluster_type_specific_id (id of the replication group for GR,
   *                                 cluster_id for ReplicaSet)
   * @param metadata_servers The list of cluster metadata servers
   * @param user_credentials MySQL Metadata username and password
   * @param ttl The time to live for the cached data
   * @param auth_cache_ttl TTL of the rest user authentication data
   * @param auth_cache_refresh_interval Refresh rate of the rest user
   *                                    authentication data
   * @param ssl_options SSL relatd options for connection
   * @param cluster_name The name of the cluster to be used.
   * @param connect_timeout The time in seconds after which trying to connect
   *                        to metadata server should time out.
   * @param read_timeout The time in seconds after which read from metadata
   *                     server should time out.
   * @param thread_stack_size memory in kilobytes allocated for thread's stack
   * @param use_cluster_notifications Flag indicating if the metadata cache
   *                                  should use cluster notifications as an
   *                                  additional trigger for metadata refresh
   *                                  (only available for GR cluster type)
   * @param view_id last known view_id of the cluster metadata (only relevant
   *                for ReplicaSet cluster)
   *
   */
  virtual void cache_init(
      const mysqlrouter::ClusterType cluster_type, const unsigned router_id,
      const std::string &cluster_type_specific_id,
      const std::vector<mysql_harness::TCPAddress> &metadata_servers,
      const mysqlrouter::UserCredentials &user_credentials,
      const std::chrono::milliseconds ttl,
      const std::chrono::milliseconds auth_cache_ttl,
      const std::chrono::milliseconds auth_cache_refresh_interval,
      const mysqlrouter::SSLOptions &ssl_options,
      const std::string &cluster_name, int connect_timeout, int read_timeout,
      size_t thread_stack_size = mysql_harness::kDefaultStackSizeInKiloBytes,
      bool use_cluster_notifications = false, const unsigned view_id = 0) = 0;

  virtual void instance_name(const std::string &inst_name) = 0;
  virtual std::string instance_name() const = 0;

  virtual bool is_initialized() noexcept = 0;

  virtual mysqlrouter::ClusterType cluster_type() const = 0;

  /**
   * @brief Start the metadata cache
   */
  virtual void cache_start() = 0;

  /**
   * @brief Teardown the metadata cache
   */
  virtual void cache_stop() noexcept = 0;

  /** @brief Returns list of managed server in a HA replicaset
   * * Returns a list of MySQL servers managed by the topology for the given
   * HA replicaset.
   *
   * @param replicaset_name ID of the HA replicaset
   * @return List of ManagedInstance objects
   */
  virtual LookupResult lookup_replicaset(
      const std::string &replicaset_name) = 0;

  /** @brief Update the status of the instance
   *
   * Called when an instance from a replicaset cannot be reached for one reason
   * or another. When an instance becomes unreachable, an emergency mode is set
   * (the rate of refresh of the metadata cache increases to once per second)
   * and lasts until disabled after a suitable change in the metadata cache is
   * discovered.
   *
   * @param instance_id - the mysql_server_uuid that identifies the server
   * instance
   * @param status - the status of the instance
   */
  virtual void mark_instance_reachability(const std::string &instance_id,
                                          InstanceStatus status) = 0;

  /** @brief Wait until there's a primary member in the replicaset
   *
   * To be called when the primary member of a single-primary replicaset is down
   * and we want to wait until one becomes elected.
   *
   * @param replicaset_name - the name of the replicaset
   * @param primary_server_uuid - server_uuid of the PRIMARY that shall be
   * failover from.
   * @param timeout - amount of time to wait for a failover, in seconds
   * @return true if a primary member exists
   */
  virtual bool wait_primary_failover(const std::string &replicaset_name,
                                     const std::string &primary_server_uuid,
                                     const std::chrono::seconds &timeout) = 0;

  /**
   * @brief Register observer that is notified when there is a change in the
   * replicaset nodes setup/state discovered.
   *
   * @param replicaset_name name of the replicaset
   * @param listener Observer object that is notified when replicaset nodes
   * state is changed.
   */
  void add_state_listener(const std::string &replicaset_name,
                          ReplicasetStateListenerInterface *listener) override =
      0;

  /**
   * @brief Unregister observer previously registered with add_state_listener()
   *
   * @param replicaset_name name of the replicaset
   * @param listener Observer object that should be unregistered.
   */
  void remove_state_listener(
      const std::string &replicaset_name,
      ReplicasetStateListenerInterface *listener) override = 0;

  /**
   * @brief Register observer that is notified when the state of listening
   * socket acceptors should be updated on the next metadata refresh.
   *
   * @param replicaset_name name of the replicaset
   * @param listener Observer object that is notified when replicaset nodes
   * state is changed.
   */
  virtual void add_acceptor_handler_listener(
      const std::string &replicaset_name,
      AcceptorUpdateHandlerInterface *listener) = 0;

  /**
   * @brief Unregister observer previously registered with
   * add_acceptor_handler_listener()
   *
   * @param replicaset_name name of the replicaset
   * @param listener Observer object that should be unregistered.
   */
  virtual void remove_acceptor_handler_listener(
      const std::string &replicaset_name,
      AcceptorUpdateHandlerInterface *listener) = 0;

  /** @brief Get authentication data (password hash and privileges) for the
   *  given user.
   *
   * @param username - name of the user for which the authentidation data
   *                   is requested
   * @return true and password hash with privileges - authentication data
   * requested for the given user.
   * @return false and empty data set - username is not found or authentication
   * data expired.
   */
  virtual std::pair<bool, std::pair<std::string, rapidjson::Document>>
  get_rest_user_auth_data(const std::string &username) const = 0;

  /**
   * @brief Enable fetching authentication metadata when using metadata_cache
   * http authentication backend.
   */
  virtual void enable_fetch_auth_metadata() = 0;

  /**
   * Force cache update in refresh loop.
   */
  virtual void force_cache_update() = 0;

  /**
   * Check values of auth_cache_ttl and auth_cache_refresh_interval timers.
   *
   * @throw std::invalid_argument for each of the following scenarios:
   * 1. auth_cache_ttl < ttl
   * 2. auth_cache_refresh_interval < ttl
   * 3. auth_cache_refresh_interval > auth_cache_ttl
   */
  virtual void check_auth_metadata_timers() const = 0;

  /**
   * Toggle socket acceptors state update on next metadata refresh.
   */
  virtual void handle_sockets_acceptors_on_md_refresh() = 0;

  MetadataCacheAPIBase() = default;
  // disable copy as it isn't needed right now. Feel free to enable
  // must be explicitly defined though.
  explicit MetadataCacheAPIBase(const MetadataCacheAPIBase &) = delete;
  MetadataCacheAPIBase &operator=(const MetadataCacheAPIBase &) = delete;
  ~MetadataCacheAPIBase() override = default;

  struct RefreshStatus {
    uint64_t refresh_failed;
    uint64_t refresh_succeeded;
    std::chrono::system_clock::time_point last_refresh_succeeded;
    std::chrono::system_clock::time_point last_refresh_failed;

    std::string last_metadata_server_host;
    uint16_t last_metadata_server_port;
  };

  virtual RefreshStatus get_refresh_status() = 0;
  virtual std::string cluster_type_specific_id() const = 0;
  virtual std::string cluster_name() const = 0;
  virtual std::chrono::milliseconds ttl() const = 0;
};

class METADATA_API MetadataCacheAPI : public MetadataCacheAPIBase {
 public:
  static MetadataCacheAPIBase *instance();

  void cache_init(
      const mysqlrouter::ClusterType cluster_type, const unsigned router_id,
      const std::string &cluster_type_specific_id,
      const std::vector<mysql_harness::TCPAddress> &metadata_servers,
      const mysqlrouter::UserCredentials &user_credentials,
      const std::chrono::milliseconds ttl,
      const std::chrono::milliseconds auth_cache_ttl,
      const std::chrono::milliseconds auth_cache_refresh_interval,
      const mysqlrouter::SSLOptions &ssl_options,
      const std::string &cluster_name, int connect_timeout, int read_timeout,
      size_t thread_stack_size, bool use_cluster_notifications,
      unsigned view_id) override;

  mysqlrouter::ClusterType cluster_type() const override;

  void instance_name(const std::string &inst_name) override;
  std::string instance_name() const override;

  std::string cluster_type_specific_id() const override;
  std::string cluster_name() const override;
  std::chrono::milliseconds ttl() const override;

  bool is_initialized() noexcept override { return is_initialized_; }
  void cache_start() override;

  void cache_stop() noexcept override;

  LookupResult lookup_replicaset(const std::string &replicaset_name) override;

  void mark_instance_reachability(const std::string &instance_id,
                                  InstanceStatus status) override;

  bool wait_primary_failover(const std::string &replicaset_name,
                             const std::string &primary_server_uuid,
                             const std::chrono::seconds &timeout) override;

  void add_state_listener(const std::string &replicaset_name,
                          ReplicasetStateListenerInterface *listener) override;

  void remove_state_listener(
      const std::string &replicaset_name,
      ReplicasetStateListenerInterface *listener) override;

  void add_acceptor_handler_listener(
      const std::string &replicaset_name,
      AcceptorUpdateHandlerInterface *listener) override;

  void remove_acceptor_handler_listener(
      const std::string &replicaset_name,
      AcceptorUpdateHandlerInterface *listener) override;

  RefreshStatus get_refresh_status() override;

  std::pair<bool, std::pair<std::string, rapidjson::Document>>
  get_rest_user_auth_data(const std::string &user) const override;

  void enable_fetch_auth_metadata() override;
  void force_cache_update() override;
  void check_auth_metadata_timers() const override;

  void handle_sockets_acceptors_on_md_refresh() override;

 private:
  struct InstData {
    std::string name;
  };
  Monitor<InstData> inst_{{}};

  std::atomic<bool> is_initialized_{false};
  MetadataCacheAPI() = default;
  MetadataCacheAPI(const MetadataCacheAPI &) = delete;
  MetadataCacheAPI &operator=(const MetadataCacheAPI &) = delete;
};

}  // namespace metadata_cache

#endif  // MYSQLROUTER_METADATA_CACHE_INCLUDED
