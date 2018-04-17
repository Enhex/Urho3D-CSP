#pragma once

#include "CSP_messages.h"
#include "StateSnapshot.h"
#include <Urho3D/Scene/Component.h>
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
Client side prediction client.

- sends input to server
- receive state snapshot from server and run prediction
*/
struct CSP_Client : Component
{
	URHO3D_OBJECT(CSP_Client, Component);

	CSP_Client(Context* context);

	using ID = unsigned;

	// Register object factory and attributes.
	static void RegisterObject(Context* context);

	
	// Fixed timestep length
	float timestep = 0;

	Controls* prediction_controls = nullptr;

	// Apply a given input locally
	std::function<void(const Controls& input, float timestep)> apply_local_input = nullptr;

	// Tags the input with "id" extraData, adds it to the input buffer, and sends it to the server.
	void add_input(Controls& input);
	
protected:
	// current client-side update ID
	ID id = 0;
	// The current recieved ID from the server
	ID server_id = -1;

	// Input buffer
	std::vector<Controls> input_buffer;
	// Reusable message buffer
	VectorBuffer input_message;

	HashMap<Scene*, StateSnapshot> scene_snapshots;


	// Handle custom network messages
	void HandleNetworkMessage(StringHash eventType, VariantMap& eventData);

	// Sends the client's input to the server
	void send_input(Controls& controls);
	// read server's last received ID
	void read_last_id(MemoryBuffer& message);


	// do client-side prediction
	void predict();

	// Re-apply all the inputs since after the current server ID to the current ID to correct the current network state.
	void reapply_inputs();

	// Remove all the elements in the buffer which are behind the server_id, including it since it was already applied.
	void remove_obsolete_history();
};
