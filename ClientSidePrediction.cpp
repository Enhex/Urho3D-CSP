#include "ClientSidePrediction.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/IO/VectorBuffer.h>
#include <Urho3D/Input/Controls.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Scene/LogicComponent.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>
#include <Urho3D/Scene/SmoothedTransform.h>

//
// Constructor
//
ClientSidePrediction::ClientSidePrediction(Context* context) :
Object(context)
{
	// Receive update messages
	SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(ClientSidePrediction, HandleNetworkMessage));

	// Send update messages
	SubscribeToEvent(E_RENDERUPDATE, URHO3D_HANDLER(ClientSidePrediction, HandleRenderUpdate));

	//
	SubscribeToEvent(E_INTERCEPTNETWORKUPDATE, URHO3D_HANDLER(ClientSidePrediction, HandleInterceptNetworkUpdate));
}


//
// RegisterObject
//
void ClientSidePrediction::RegisterObject(Context* context)
{
	context->RegisterFactory<ClientSidePrediction>();
}


//
// add_node
//
void ClientSidePrediction::add_node(Node* node)
{
	scene_snapshots[node->GetScene()].add_node(node);
}


//
// add_input
//
void ClientSidePrediction::add_input(Controls& new_input)
{
	// Increment the update ID by 1
	++id;
	// Tag the new input with an id, so the id is passed to the server
	new_input.extraData_["id"] = id;
	// Add the new input to the input buffer
	input_buffer.push_back(new_input);

	// Send to the server
	send_input(new_input);
}


//
// send_input
//
void ClientSidePrediction::send_input(Controls& controls)
{
	auto server_connection = GetSubsystem<Network>()->GetServerConnection();
	if (!server_connection ||
		!server_connection->GetScene() ||
		!server_connection->IsSceneLoaded())
		return;

	input_message.Clear();
	input_message.WriteUInt(controls.buttons_);
	input_message.WriteFloat(controls.yaw_);
	input_message.WriteFloat(controls.pitch_);
	input_message.WriteVariantMap(controls.extraData_);

	// No access, and currently no use for position optimization
	/*if (sendMode_ >= OPSM_POSITION)
		input_message.WriteVector3(position_);
	if (sendMode_ >= OPSM_POSITION_ROTATION)
		input_message.WritePackedQuaternion(rotation_);*/

	server_connection->SendMessage(MSG_CSP_INPUT, false, false, input_message);
}


//
// HandleNetworkMessage
//
void ClientSidePrediction::HandleNetworkMessage(StringHash eventType, VariantMap& eventData)
{
	auto network = GetSubsystem<Network>();

	using namespace NetworkMessage;
	const auto message_id = eventData[P_MESSAGEID].GetInt();
	auto connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
	MemoryBuffer message(eventData[P_DATA].GetBuffer());

	if (network->IsServerRunning())
	{
		switch (message_id)
		{
		case MSG_CSP_INPUT:
			read_input(connection, message);
			break;
		}
	}
	
	else if (network->GetServerConnection())
	{
		switch (message_id)
		{
		case MSG_CSP_STATE:
			// read last input
			read_last_id(message);
			// read state snapshot
			auto scene = network->GetServerConnection()->GetScene();
			scene_snapshots[scene].read_state(message, scene);

			// Perform client side prediction
			predict();

			break;
		}
	}
}


//
// HandleRenderUpdate
//
void ClientSidePrediction::HandleRenderUpdate(StringHash eventType, VariantMap& eventData)
{
	auto network = GetSubsystem<Network>();

	auto timeStep = eventData[RenderUpdate::P_TIMESTEP].GetFloat();
	
	// Check if periodic update should happen now
	updateAcc_ += timeStep;
	bool updateNow = updateAcc_ >= updateInterval_;

	if (updateNow && network->IsServerRunning())
	{
		updateAcc_ = fmodf(updateAcc_, updateInterval_);

		prepare_state_snapshots();
		send_state_updates();
	}
}

void ClientSidePrediction::HandleInterceptNetworkUpdate(StringHash eventType, VariantMap & eventData)
{
	using namespace InterceptNetworkUpdate;
	auto name = eventData[P_NAME].GetString();
	int t = 1;
}


void ClientSidePrediction::read_input(Connection* connection, MemoryBuffer& message)
{
	auto network = GetSubsystem<Network>();

	if (!connection->IsClient())
	{
		URHO3D_LOGWARNING("Received unexpected Controls message from server");
		return;
	}

	Controls newControls;
	newControls.buttons_	= message.ReadUInt();
	newControls.yaw_		= message.ReadFloat();
	newControls.pitch_		= message.ReadFloat();
	newControls.extraData_	= message.ReadVariantMap();

	client_input_IDs[connection] = newControls.extraData_["id"].GetUInt();

	apply_client_input(newControls, timestep, connection);

	// No access, and currently no use
	//// Client may or may not send observer position & rotation for interest management
	//if (!msg.IsEof())
	//	position_ = msg.ReadVector3();
	//if (!msg.IsEof())
	//	rotation_ = msg.ReadPackedQuaternion();
}


void ClientSidePrediction::read_last_id(MemoryBuffer& message)
{
	// Read last input ID
	auto new_server_id = message.ReadUInt();

	// Make sure it's more recent than the previous last ID since we're sending unordered messages
	// Handle range looping correctly
	if (id > server_id) {
		if (new_server_id < server_id)
			return;
	}
	else {
		if (new_server_id > server_id)
			return;
	}

	server_id = new_server_id;
}


//
// prepare_state_snapshots
//
void ClientSidePrediction::prepare_state_snapshots()
{
	auto network = GetSubsystem<Network>();
	auto client_connections = network->GetClientConnections();

	// Collect all networked scenes
	network_scenes.Clear();
	for (auto i = client_connections.Begin(); i != client_connections.End(); ++i)
	{
		Scene* scene = (*i)->GetScene();
		if (scene)
			network_scenes.Insert(scene);
	}

	// Prepare all networked scenes
	for (auto i = network_scenes.Begin(); i != network_scenes.End(); ++i)
	{
		auto scene = (*i);

		auto& state_message = scene_states[scene];
		state_message.Clear();

		// Write placeholder last input ID, which will be set per connection before sending
		state_message.WriteUInt(0);

		// write state snapshot
		auto& snapshot = scene_snapshots[scene];
		snapshot.write_state(state_message, scene);
	}
}


//
// send_state_updates
//
void ClientSidePrediction::send_state_updates()
{
	auto network = GetSubsystem<Network>();
	auto client_connections = network->GetClientConnections();

	for (auto i = client_connections.Begin(); i != client_connections.End(); ++i)
		send_state_update((*i));
}


//
// send_state_update
//
void ClientSidePrediction::send_state_update(Connection* connection)
{
	// Set the last input ID per connection
	unsigned int last_id = client_input_IDs[connection];

	auto& state = scene_states[connection->GetScene()];
	state.Seek(0);
	state.WriteUInt(last_id);
	
	connection->SendMessage(MSG_CSP_STATE, false, false, state);
}


/* Only create nodes, dont update version for debugging
void ClientSidePrediction::read_node(MemoryBuffer& message)
{
	auto network = GetSubsystem<Network>();
	auto scene = network->GetServerConnection()->GetScene();

	auto node_id = message.ReadUInt();
	auto node = scene->GetNode(node_id);
	bool new_node = false;

	// Create the node if it doesn't exist
	if (!node)
	{
		new_node = true;
		// Add initially to the root level. May be moved as we receive the parent attribute
		node = scene->CreateChild(node_id, LOCAL);
		// Create smoothed transform component
		node->CreateComponent<SmoothedTransform>(LOCAL);

		// Read attributes
		read_network_attributes(*node, message);
		// ApplyAttributes() is deliberately skipped, as Node has no attributes that require late applying.
		// Furthermore it would propagate to components and child nodes, which is not desired in this case

		// Snap the motion smoothing immediately to the end
		auto transform = node->GetComponent<SmoothedTransform>();
		if (transform)
			transform->Update(1.0f, 0.0f);

		// Read user variables
		unsigned num_vars = message.ReadVLE();
		for (; num_vars > 0; --num_vars)
		{
			auto key = message.ReadStringHash();
			node->SetVar(key, message.ReadVariant());
		}

		// Read components
		unsigned num_components = message.ReadVLE();
		for (; num_components > 0; --num_components)
			read_component(message, node);
	}
	else
	{
		// Remove the node from the unused nodes list
		unused_nodes.erase(node);
	}
}
*/


void ClientSidePrediction::predict()
{
	remove_obsolete_history();
	reapply_inputs();
}


void ClientSidePrediction::reapply_inputs()
{
	for (auto& controls : input_buffer)
	{
		if (unsigned(controls.extraData_["id"].GetUInt()) > server_id)
			apply_local_input(controls, timestep);
	}
}


void ClientSidePrediction::remove_obsolete_history()
{
	input_buffer.erase(
		std::remove_if(input_buffer.begin(), input_buffer.end(), [&](Controls& controls) {
			unsigned update_id = controls.extraData_["id"].GetUInt();
			// Handle value range looping correctly
			if (id >= server_id)
			{
				if (update_id <= server_id ||
					update_id > id)
					return true;
			}
			else
			{
				if (update_id >= server_id ||
					update_id < id)
					return true;
			}
			return false;
		}),
		input_buffer.end());
}
