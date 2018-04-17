#include "CSP_Server.h"

#include <Urho3D/Core/Context.h>
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/DebugHud.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/MemoryBuffer.h>
#include <Urho3D/Input/Controls.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/Scene/SceneEvents.h>

CSP_Server::CSP_Server(Context * context) :
	Component(context)
{
	// Receive update messages
	SubscribeToEvent(E_NETWORKMESSAGE, URHO3D_HANDLER(CSP_Server, HandleNetworkMessage));

	// Send update messages
	SubscribeToEvent(E_RENDERUPDATE, URHO3D_HANDLER(CSP_Server, HandleRenderUpdate));
}

void CSP_Server::RegisterObject(Context * context)
{
	context->RegisterFactory<CSP_Server>();
}

void CSP_Server::add_node(Node * node)
{
	scene_snapshots[node->GetScene()].add_node(node);
}

void CSP_Server::HandleNetworkMessage(StringHash eventType, VariantMap & eventData)
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
			URHO3D_LOGDEBUG("MSG_CSP_INPUT");
			read_input(connection, message);
			break;
		}
	}
}

void CSP_Server::HandleRenderUpdate(StringHash eventType, VariantMap & eventData)
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

void CSP_Server::read_input(Connection * connection, MemoryBuffer & message)
{
	auto network = GetSubsystem<Network>();

	if (!connection->IsClient())
	{
		URHO3D_LOGWARNING("Received unexpected Controls message from server");
		return;
	}

	Controls newControls;
	newControls.buttons_ = message.ReadUInt();
	newControls.yaw_ = message.ReadFloat();
	newControls.pitch_ = message.ReadFloat();
	newControls.extraData_ = message.ReadVariantMap();

	if(newControls.extraData_["id"].GetUInt() > client_inputs[connection].extraData_["id"].GetUInt())
		client_inputs[connection] = newControls;

	// testing applying input in PreStep
	//client_input_IDs[connection] = newControls.extraData_["id"].GetUInt();
	//apply_client_input(newControls, timestep, connection);

	// No access, and currently no use
	//// Client may or may not send observer position & rotation for interest management
	//if (!msg.IsEof())
	//	position_ = msg.ReadVector3();
	//if (!msg.IsEof())
	//	rotation_ = msg.ReadPackedQuaternion();
}

void CSP_Server::prepare_state_snapshots()
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

		GetSubsystem<DebugHud>()->SetAppStats("snapshots_sent: ", ++snapshots_sent);
	}
}

void CSP_Server::send_state_updates()
{
	auto network = GetSubsystem<Network>();
	auto client_connections = network->GetClientConnections();

	for (auto i = client_connections.Begin(); i != client_connections.End(); ++i)
		send_state_update((*i));
}

void CSP_Server::send_state_update(Connection * connection)
{
	// Set the last input ID per connection
	unsigned int last_id = client_input_IDs[connection];

	auto& state = scene_states[connection->GetScene()];
	state.Seek(0);
	state.WriteUInt(last_id);

	connection->SendMessage(MSG_CSP_STATE, false, false, state);
}
