#pragma once

#include "CSP_Server.h"
#include "CSP_messages.h"
#include "StateSnapshot.h"
#include <Urho3D/Scene/Component.h>
#include <queue>
#include <vector>

namespace Urho3D
{
	class Context;
	class Controls;
	class Connection;
	class MemoryBuffer;
}

using namespace Urho3D;


/*
Client side prediction server.

- receive inputs from clients
- keep track of each client's last input ID
- sends last used input ID
- sends state snapshot
*/
struct CSP_Server : Component
{
	URHO3D_OBJECT(CSP_Server, Component);

	CSP_Server(Context* context);

	using ID = unsigned;

	// Register object factory and attributes.
	static void RegisterObject(Context* context);


	// Fixed timestep length
	float timestep = 0;


	// Client input ID map
	HashMap<Connection*, ID> client_input_IDs;
	HashMap<Connection*, std::queue<Controls>> client_inputs;//TODO if using queue, use a getter


	// Add a node to the client side prediction
	void add_node(Node* node);


protected:
	// Networked scenes
	HashSet<Scene*> network_scenes;

	// State snapshot of each scene
	HashMap<Scene*, VectorBuffer> scene_states;
	HashMap<Scene*, StateSnapshot> scene_snapshots;

	// for debugging
	unsigned snapshots_sent = 0;

	// Handle custom network messages
	void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);
	// Send state snapshots
	void HandleRenderUpdate(StringHash eventType, VariantMap& eventData);

	// Read input sent from the client and apply it
	void read_input(Connection* connection, MemoryBuffer& message);

	/*
	serialization structure:
	- Last input ID
	- state snapshot
	*/
	// Prepare state snapshot for each networked scene
	void prepare_state_snapshots();
	// For each connection send the last received input ID and scene state snapshot
	void send_state_updates();
	// Send a state update to a given connection
	void send_state_update(Connection* connection);


private:
	// Update time interval
	// Update time accumulator
	float updateAcc_ = 0;
public:
	//TODO same as timestep?
	float updateInterval_ = 1.f / 30.f;	// default to 30 FPS
};
