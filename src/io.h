// This file is part of playd.
// playd is licensed under the MIT licence: see LICENSE.txt.

/**
 * @file
 * Declaration of the I/O classes used in playd.
 * @see io.cpp
 */

#ifndef PLAYD_IO_CORE_H
#define PLAYD_IO_CORE_H

#include <ostream>
#include <set>

// Use the same ssize_t as libmpg123 on Windows.
#ifdef _MSC_VER
typedef ptrdiff_t ssize_t;
#define _SSIZE_T_
#define _SSIZE_T_DEFINED
#endif
#include <uv.h>

#include "player.h"
#include "response.h"
#include "tokeniser.h"

// Forward declaration needed because of cyclic dependency between playd::io::Core and playd::Player.
namespace Playd
{
class Player;
}

namespace Playd::IO
{

class Connection;

/**
 * The IO core, which services input, routes responses, and executes the
 * Player update routine periodically.
 *
 * The IO core also maintains a pool of connections which can be sent responses
 * via their IDs inside the pool.  It ensures that each connection is given an
 * ID that is unique up until the removal of said connection.
 */
class Core : public ResponseSink
{
public:
	/**
	 * Constructs an IO core.
	 * @param player The player to which update requests, commands, and new
	 *   connection state dump requests shall be sent.
	 */
	explicit Core(Player &player);

	/// Deleted copy constructor.
	Core(const Core &) = delete;

	/// Deleted copy-assignment.
	Core &operator=(const Core &) = delete;

	/**
	 * Runs the reactor.
	 * It will block until it terminates.
	 * @param host The IP host to which the IO core will bind.
	 * @param port The TCP port to which the IO core will bind.
	 * @exception Net_error Thrown if the IO core cannot bind to @a host or @a
	 *   port.
	 */
	void Run(std::string_view host, std::string_view port);

	//
	// Connection API
	//

	/**
	 * Accepts a new connection.
	 *
	 * This accepts the connection, and adds it to this IoCore's
	 * connection pool.
	 *
	 * This should be called with a server that has just received a new
	 * connection.
	 *
	 * @param server Pointer to the libuv server accepting connections.
	 */
	void Accept(uv_stream_t *server);

	/**
	 * Removes a connection.
	 * As the IoCore owns the Connection, it will be destroyed by this
	 * operation.
	 * @param id The ID of the connection to remove.
	 */
	void Remove(ClientId id);

	/**
	 * Performs a player update cycle.
	 * If the player is closing, IoCore will announce this fact to
	 * all current connections, close them, and end the I/O loop.
	 */
	void UpdatePlayer();

	void Respond(ClientId id, const Response &response) const override;

	/// Shuts down the IoCore by terminating all IO loop tasks.
	void Shutdown();

private:
	/// The period between player updates.
	static const uint16_t PLAYER_UPDATE_PERIOD;

	uv_loop_t *loop;      ///< The loop this IoCore is using.
	uv_signal_t sigint{}; ///< The libuv handle for the Ctrl-C signal.
	uv_tcp_t server{};    ///< The libuv handle for the TCP server.
	uv_timer_t updater{}; ///< The libuv handle for the update timer.

	Player &player; ///< The player.

	/// The set of connections inside this IoCore.
	std::vector<std::shared_ptr<Connection>> pool;

	/// A list of free 1-indexed slots inside pool.
	/// These slots may be re-used instead of creating a new slot.
	std::vector<ClientId> free_list;

	/**
	 * Initialises a TCP acceptor on the given address and port.
	 *
	 * @param address The IPv4 address on which the TCP server should
	 *   listen.
	 * @param port The TCP port on which the TCP server should listen.
	 */
	void InitAcceptor(std::string_view address, std::string_view port);

	/// Sets up a periodic timer to run the playd update loop.
	void InitUpdateTimer();

	/**
	 * Initialises playd's signal handling.
	 *
	 * We trap SIGINT, and the equivalent emulated signal on Windows, to
	 * make playd close gracefully when Ctrl-C is sent.
	 */
	void InitSignals();

	//
	// Connection pool handling
	//

	/**
	 * Acquires the next available connection ID.
	 * This ID may have been assigned to a connection in the past, but is
	 * guaranteed not to match any currently assigned IDs.
	 * @return size_t A fresh ID for use.
	 */
	ClientId NextConnectionID();

	/**
	 * Adds a new connection slot to the connection pool.
	 * This is called by NextConnectionID when the number of currently
	 * running connections is larger than the number of existing pool
	 * slots, and will eventually fail (when the number of simultaneous
	 * connections reaches absurd proportions).
	 */
	void ExpandPool();

	//
	// Response dispatch
	//

	/**
	 * Sends the given response to all connections.
	 * @param response The response to broadcast.
	 */
	void Broadcast(const Response &response) const;

	/**
	 * Sends the given response to the identified connection.
	 * @param id The ID of the recipient connection.
	 * @param response The response to broadcast.
	 */
	void Unicast(ClientId id, const Response &response) const;

	/**
	 * Sends the initial responses to the given connection.
	 * @param id The ID of the recipient connection.
	 */
	void SendInitialResponses(ClientId id) const;
};

/**
 * A TCP connection from a client.
 *
 * This class wraps a libuv TCP stream representing a client connection,
 * allowing it to be sent responses (directly, or via a broadcast), removed
 * from its IoCore, and queried for its name.
 */
class Connection
{
public:
	/**
	 * Constructs a Connection.
	 * @param parent The connection pool to which this Connection belongs.
	 * @param tcp The underlying libuv TCP stream.
	 * @param player The player to which read commands should be sent.
	 * @param id The ID of this Connection in the IoCore.
	 */
	Connection(Core &parent, uv_tcp_t *tcp, Player &player, ClientId id);

	/**
	 * Destructs a Connection.
	 * This causes libuv to close and free the libuv TCP stream.
	 */
	~Connection();

	/// Connection cannot be copied.
	Connection(const Connection &) = delete;

	/// Connection cannot be copy-assigned.
	Connection &operator=(const Connection &) = delete;

	/**
	 * Emits a Response via this Connection.
	 * @param response The response to send.
	 */
	void Respond(const Response &response);

	/**
	 * Processes a data read on this connection.
	 * @param nread The number of bytes read.
	 * @param buf The buffer containing the read data.
	 */
	void Read(ssize_t nread, const uv_buf_t *buf);

	/**
	 * Gracefully shuts this connection down.
	 *
	 * This is similar to (and implemented in terms of) Depool,
	 * but waits for all writes to finish first.
	 */
	void Shutdown();

	/**
	 * Removes this connection from its connection pool.
	 * Since the pool may contain a shared reference to this connection,
	 * calling this can result in the connection being destructed.
	 */
	void Depool();

	/**
	 * Retrieves a name for this connection.
	 * This will be of the form "HOST:PORT", unless errors occur.
	 * @return The Connection's name.
	 */
	std::string Name();

private:
	/// The pool on which this connection is running.
	Core &parent;

	/// The libuv handle for the TCP connection.
	uv_tcp_t *tcp;

	/// The Tokeniser to which data read on this connection should be sent.
	Tokeniser tokeniser;

	/// The Player to which finished commands should be sent.
	Player &player;

	/// The Connection's ID in the connection pool.
	ClientId id;

	/**
	 * Handles a tokenised command line.
	 * @param msg A vector of command words representing a command line.
	 * @return A final response returning whether the command succeeded.
	 */
	Response RunCommand(const std::vector<std::string> &msg);
};

} // namespace Playd::IO

#endif // PLAYD_IO_CORE_H
