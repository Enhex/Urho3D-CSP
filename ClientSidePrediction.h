#pragma once

#include "CSP_messages.h"
#include "StateSnapshot.h"
#include <Urho3D/Container/HashSet.h>
#include <Urho3D/Core/Object.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <functional>
#include <unordered_set>
#include <vector>

namespace Urho3D
{
	class Context;
	class Serializable;
	class Controls;
	class Component;
	class Scene;
	class Node;
	class Connection;
	class MemoryBuffer;
}

using namespace Urho3D;

/*
Client side prediction subsystem.
Works alongside the Network subsystem.
Usage:
Add LOCAL nodes which you want to be predicted.
Note: Uses the PhysicsWorld Fps as a fixed timestep.
*/
struct ClientSidePrediction : Object
{
	URHO3D_OBJECT(ClientSidePrediction, Object)

	using ID = unsigned;

	ClientSidePrediction(Context* context);

	// Register object factory and attributes.
	static void RegisterObject(Context* context);


	// Fixed timestep length
	float timestep = 0;

	// Server: Add a node to the client side prediction
	void add_node(Node* node);

	// Apply a given input locally
	std::function<void(const Controls& input, float timestep)> apply_local_input = nullptr;
	// Apply a given input to a specific client
	std::function<void(const Controls& input, float timestep, Connection* connection)> apply_client_input = nullptr;

	// Tags the input with "id" extraData, adds it to the input buffer, and sends it to the server.
	void add_input(Controls& input);


protected:
	// Networked scenes
	HashSet<Scene*> network_scenes;

	// State snapshot of each scene
	HashMap<Scene*, VectorBuffer> scene_states;
	HashMap<Scene*, StateSnapshot> scene_snapshots;
	
	// current client-side update ID
	ID id = 0;
	// The current recieved ID from the server
	ID server_id = -1;

	// Input buffer
	std::vector<Controls> input_buffer;
	// Reusable message buffer
	VectorBuffer input_message;
	// Client input ID map
	HashMap<Connection*, ID> client_input_IDs;


	// Handle custom network messages
	void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);
	// Send state snapshots
	void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);

	void HandleInterceptNetworkUpdate(StringHash eventType, VariantMap& eventData);

	// Sends the client's input to the server
	void send_input(Controls& controls);
	// Read input sent from the client and apply it
	void read_input(Connection* connection, MemoryBuffer& message);

	// read server's last received ID
	void read_last_id(MemoryBuffer& message);

	// Prepare state snapshot for each networked scene
	void prepare_state_snapshots();
	// For each connection send the last received input ID and scene state snapshot
	void send_state_updates();
	// Send a state update to a given connection
	void send_state_update(Connection* connection);
	/*
	serialization structure:
	- Last input ID
	- state snapshot
	*/

	// do client-side prediction
	void predict();

	// Re-apply all the inputs since after the current server ID to the current ID to correct the current network state.
	void reapply_inputs();

	// Remove all the elements in the buffer which are behind the server_id, including it since it was already applied.
	void remove_obsolete_history();

private:
	// Update time interval
	//TODO same as timestep?
	float updateInterval_ = 1.f / 30.f;	// default to 30 FPS
	// Update time accumulator
	float updateAcc_ = 0;
};
