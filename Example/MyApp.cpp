#include "MyApp.h"

#include "../CSP_Client.h"
#include "../CSP_Server.h"
#include <Urho3D/Core/CoreEvents.h>
#include <Urho3D/Engine/Console.h>
#include <Urho3D/Engine/DebugHud.h>
#include <Urho3D/Engine/Engine.h>
#include <Urho3D/Graphics/Camera.h>
#include <Urho3D/Graphics/DebugRenderer.h>
#include <Urho3D/Graphics/Graphics.h>
#include <Urho3D/Graphics/Light.h>
#include <Urho3D/Graphics/Material.h>
#include <Urho3D/Graphics/Model.h>
#include <Urho3D/Graphics/Octree.h>
#include <Urho3D/Graphics/Renderer.h>
#include <Urho3D/Graphics/StaticModel.h>
#include <Urho3D/Graphics/Zone.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/Input/Input.h>
#include <Urho3D/Input/InputEvents.h>
#include <Urho3D/Network/Connection.h>
#include <Urho3D/Network/Network.h>
#include <Urho3D/Network/NetworkEvents.h>
#include <Urho3D/Physics/CollisionShape.h>
#include <Urho3D/Physics/PhysicsEvents.h>
#include <Urho3D/Physics/PhysicsWorld.h>
#include <Urho3D/Physics/RigidBody.h>
#include <Urho3D/Resource/ResourceCache.h>
#include <Urho3D/Scene/Scene.h>
#include <Urho3D/UI/Button.h>
#include <Urho3D/UI/Font.h>
#include <Urho3D/UI/LineEdit.h>
#include <Urho3D/UI/Text.h>
#include <Urho3D/UI/UI.h>
#include <Urho3D/UI/UIEvents.h>


//#define CSP_DEBUG


// UDP port we will use
static const unsigned short SERVER_PORT = 2354;
// Identifier for our custom remote event we use to tell the client which object they control
static const StringHash E_CLIENTOBJECTID("ClientObjectID");
// Identifier for the node ID parameter in the event data
static const StringHash P_ID("ID");

// Control bits we define
static const unsigned CTRL_FORWARD = 1;
static const unsigned CTRL_BACK = 2;
static const unsigned CTRL_LEFT = 4;
static const unsigned CTRL_RIGHT = 8;


//
// Constructor
//
MyApp::MyApp(Context* context) :
Application(context)
{
	CSP_Client::RegisterObject(context);
	CSP_Server::RegisterObject(context);
}


//
// Setup
//
void MyApp::Setup()
{
	engineParameters_["WindowWidth"] = 800;
	engineParameters_["WindowHeight"] = 600;
	engineParameters_["FullScreen"] = false;
}


//
// Start
//
void MyApp::Start()
{
	auto cache = GetSubsystem<ResourceCache>();

	// Create the scene content
	CreateScene();

	// Create the UI content
	CreateUI();

	// Setup the viewport for displaying the scene
	SetupViewport();

	// Hook up to necessary events
	SubscribeToEvents();
}

void MyApp::CreateScene()
{
	scene = MakeShared<Scene>(context_);

	auto cache = GetSubsystem<ResourceCache>();

	scene->CreateComponent<DebugRenderer>(LOCAL);

	// Create octree and physics world with default settings. Create them as local so that they are not needlessly replicated
	// when a client connects
	scene->CreateComponent<Octree>(LOCAL);
	auto physicsWorld = scene->CreateComponent<PhysicsWorld>(LOCAL);
	physicsWorld->SetInterpolation(false); // needed for determinism
#ifdef CSP_DEBUG
	physicsWorld->SetFps(10);
#endif
	// All static scene content and the camera are also created as local, so that they are unaffected by scene replication and are
	// not removed from the client upon connection. Create a Zone component first for ambient lighting & fog control.
	auto zoneNode = scene->CreateChild("Zone", LOCAL);
	auto zone = zoneNode->CreateComponent<Zone>();
	zone->SetBoundingBox(BoundingBox(-1000.0f, 1000.0f));
	zone->SetAmbientColor(Color(0.1f, 0.1f, 0.1f));
	zone->SetFogStart(100.0f);
	zone->SetFogEnd(300.0f);

	// Create a directional light without shadows
	auto lightNode = scene->CreateChild("DirectionalLight", LOCAL);
	lightNode->SetDirection(Vector3(0.5f, -1.0f, 0.5f));
	auto light = lightNode->CreateComponent<Light>();
	light->SetLightType(LIGHT_DIRECTIONAL);
	light->SetColor(Color(0.2f, 0.2f, 0.2f));
	light->SetSpecularIntensity(1.0f);

	// Create a "floor" consisting of several tiles. Make the tiles physical but leave small cracks between them
	for (int y = -20; y <= 20; ++y)
	{
		for (int x = -20; x <= 20; ++x)
		{
			auto floorNode = scene->CreateChild("FloorTile", LOCAL);
			floorNode->SetPosition(Vector3(x * 20.2f, -0.5f, y * 20.2f));
			floorNode->SetScale(Vector3(20.0f, 1.0f, 20.0f));
			auto floorObject = floorNode->CreateComponent<StaticModel>();
			floorObject->SetModel(cache->GetResource<Model>("Models/Box.mdl"));
			floorObject->SetMaterial(cache->GetResource<Material>("Materials/Stone.xml"));

			auto body = floorNode->CreateComponent<RigidBody>();
			body->SetFriction(1.0f);
			auto shape = floorNode->CreateComponent<CollisionShape>();
			shape->SetBox(Vector3::ONE);
		}
	}

	// Create the camera. Limit far clip distance to match the fog
	// The camera needs to be created into a local node so that each client can retain its own camera, that is unaffected by
	// network messages. Furthermore, because the client removes all replicated scene nodes when connecting to a server scene,
	// the screen would become blank if the camera node was replicated (as only the locally created camera is assigned to a
	// viewport in SetupViewports() below)
	cameraNode = scene->CreateChild("Camera", LOCAL);
	auto camera = cameraNode->CreateComponent<Camera>();
	camera->SetFarClip(300.0f);

	// Set an initial position for the camera scene node above the plane
	cameraNode->SetPosition(Vector3(0.0f, 5.0f, 0.0f));
}

void MyApp::SetupViewport()
{
	auto renderer = GetSubsystem<Renderer>();

	// Set up a viewport to the Renderer subsystem so that the 3D scene can be seen
	auto camera = scene->GetChild("Camera")->GetComponent<Camera>();
	auto viewport = MakeShared<Viewport>(context_, scene, camera);
	renderer->SetViewport(0, viewport);
}

void MyApp::SubscribeToEvents()
{
	SubscribeToEvent(E_KEYDOWN, URHO3D_HANDLER(MyApp, HandleKeyDown));

	// Subscribe to fixed timestep physics updates for setting or applying controls
	SubscribeToEvent(E_PHYSICSPRESTEP, URHO3D_HANDLER(MyApp, HandlePhysicsPreStep));

	// Subscribe HandlePostUpdate() method for processing update events. Subscribe to PostUpdate instead
	// of the usual Update so that physics simulation has already proceeded for the frame, and can
	// accurately follow the object with the camera
	SubscribeToEvent(E_POSTUPDATE, URHO3D_HANDLER(MyApp, HandlePostUpdate));

	// Subscribe to button actions
	SubscribeToEvent(connectButton_, E_RELEASED, URHO3D_HANDLER(MyApp, HandleConnect));
	SubscribeToEvent(disconnectButton_, E_RELEASED, URHO3D_HANDLER(MyApp, HandleDisconnect));
	SubscribeToEvent(startServerButton_, E_RELEASED, URHO3D_HANDLER(MyApp, HandleStartServer));

	// Subscribe to network events
	SubscribeToEvent(E_SERVERCONNECTED, URHO3D_HANDLER(MyApp, HandleConnectionStatus));
	SubscribeToEvent(E_SERVERDISCONNECTED, URHO3D_HANDLER(MyApp, HandleConnectionStatus));
	SubscribeToEvent(E_CONNECTFAILED, URHO3D_HANDLER(MyApp, HandleConnectionStatus));
	SubscribeToEvent(E_CLIENTCONNECTED, URHO3D_HANDLER(MyApp, HandleClientConnected));
	SubscribeToEvent(E_CLIENTDISCONNECTED, URHO3D_HANDLER(MyApp, HandleClientDisconnected));
	// This is a custom event, sent from the server to the client. It tells the node ID of the object the client should control
	SubscribeToEvent(E_CLIENTOBJECTID, URHO3D_HANDLER(MyApp, HandleClientObjectID));
	// Events sent between client & server (remote events) must be explicitly registered or else they are not allowed to be received
	GetSubsystem<Network>()->RegisterRemoteEvent(E_CLIENTOBJECTID);
}

void MyApp::CreateUI()
{
	auto cache = GetSubsystem<ResourceCache>();
	auto ui = GetSubsystem<UI>();
	auto root = ui->GetRoot();
	auto uiStyle = cache->GetResource<XMLFile>("UI/DefaultStyle.xml");
	// Set style to the UI root so that elements will inherit it
	root->SetDefaultStyle(uiStyle);

	auto graphics = GetSubsystem<Graphics>();

	// Create console
	auto console = engine_->CreateConsole();
	console->SetDefaultStyle(uiStyle);
	console->GetBackground()->SetOpacity(0.8f);

	// Create debug HUD
	auto debugHud = engine_->CreateDebugHud();
	debugHud->SetDefaultStyle(uiStyle);
	debugHud->SetProfilerInterval(1.f / scene->GetComponent<PhysicsWorld>()->GetFps());

	// Construct the instructions text element
	instructionsText_ = ui->GetRoot()->CreateChild<Text>();
	instructionsText_->SetText(
		"Use WASD keys to move and RMB to rotate view"
	);
	instructionsText_->SetFont(cache->GetResource<Font>("Fonts/Anonymous Pro.ttf"), 15);
	// Position the text relative to the screen center
	instructionsText_->SetHorizontalAlignment(HA_CENTER);
	instructionsText_->SetVerticalAlignment(VA_CENTER);
	instructionsText_->SetPosition(0, graphics->GetHeight() / 4);
	// Hide until connected
	instructionsText_->SetVisible(false);

	buttonContainer_ = root->CreateChild<UIElement>();
	buttonContainer_->SetFixedSize(500, 20);
	buttonContainer_->SetPosition(20, 20);
	buttonContainer_->SetLayoutMode(LM_HORIZONTAL);

	textEdit_ = buttonContainer_->CreateChild<LineEdit>();
	textEdit_->SetStyleAuto();

	connectButton_ = CreateButton("Connect", 90);
	disconnectButton_ = CreateButton("Disconnect", 100);
	startServerButton_ = CreateButton("Start Server", 110);

	UpdateButtons();
}

Button * MyApp::CreateButton(const String & text, int width)
{
	auto cache = GetSubsystem<ResourceCache>();
	Font* font = cache->GetResource<Font>("Fonts/Anonymous Pro.ttf");

	auto button = buttonContainer_->CreateChild<Button>();
	button->SetStyleAuto();
	button->SetFixedWidth(width);

	Text* buttonText = button->CreateChild<Text>();
	buttonText->SetFont(font, 12);
	buttonText->SetAlignment(HA_CENTER, VA_CENTER);
	buttonText->SetText(text);

	return button;
}

void MyApp::UpdateButtons()
{
	auto network = GetSubsystem<Network>();
	auto serverConnection = network->GetServerConnection();
	const auto serverRunning = network->IsServerRunning();

	// Show and hide buttons so that eg. Connect and Disconnect are never shown at the same time
	connectButton_->SetVisible(!serverConnection && !serverRunning);
	disconnectButton_->SetVisible(serverConnection || serverRunning);
	startServerButton_->SetVisible(!serverConnection && !serverRunning);
	textEdit_->SetVisible(!serverConnection && !serverRunning);
}

Node * MyApp::CreateControllableObject()
{
	auto cache = GetSubsystem<ResourceCache>();

	// Create the scene node & visual representation. This will be a replicated object
	auto ballNode = scene->CreateChild("Ball", LOCAL);
	ballNode->SetPosition({ Random(40.0f) - 20.0f, 2.0f, Random(40.0f) - 20.0f });
	ballNode->SetScale(0.5f);
	auto ballObject = ballNode->CreateComponent<StaticModel>();
	ballObject->SetModel(cache->GetResource<Model>("Models/Sphere.mdl"));
	ballObject->SetMaterial(cache->GetResource<Material>("Materials/StoneSmall.xml"));

	// Create the physics components
	auto body = ballNode->CreateComponent<RigidBody>();
	body->SetMass(1.0f);
	body->SetFriction(1.0f);
	// In addition to friction, use motion damping so that the ball can not accelerate limitlessly
	body->SetLinearDamping(0.5f);
	body->SetAngularDamping(0.5f);
	auto shape = ballNode->CreateComponent<CollisionShape>();
	shape->SetSphere(1.0f);

	// Create a random colored point light at the ball so that can see better where is going
	auto light = ballNode->CreateComponent<Light>();
	light->SetRange(3.0f);
	light->SetColor(Color(0.5f + (Rand() & 1) * 0.5f, 0.5f + (Rand() & 1) * 0.5f, 0.5f + (Rand() & 1) * 0.5f));

	auto csp = scene->GetComponent<CSP_Server>();
	csp->add_node(ballNode);

	return ballNode;
}

void MyApp::MoveCamera()
{
	// Right mouse button controls mouse visibility: hide when pressed
	auto ui = GetSubsystem<UI>();
	auto input = GetSubsystem<Input>();
	input->SetMouseVisible(!input->GetMouseButtonDown(MOUSEB_RIGHT));

	// Mouse sensitivity as degrees per pixel
	const float MOUSE_SENSITIVITY = 0.1f;

	// Use this frame's mouse motion to adjust camera node yaw and pitch. Clamp the pitch and only move the camera
	// when the mouse is hidden
	if (!input->IsMouseVisible())
	{
		IntVector2 mouseMove = input->GetMouseMove();
		yaw_ += MOUSE_SENSITIVITY * mouseMove.x_;
		pitch_ += MOUSE_SENSITIVITY * mouseMove.y_;
		pitch_ = Clamp(pitch_, 1.0f, 90.0f);
	}

	// Construct new orientation for the camera scene node from yaw and pitch. Roll is fixed to zero
	cameraNode->SetRotation(Quaternion(pitch_, yaw_, 0.0f));

	// Only move the camera / show instructions if we have a controllable object
	bool showInstructions = false;
	if (clientObjectID_)
	{
		auto ballNode = scene->GetNode(clientObjectID_);
		if (ballNode)
		{
			constexpr float CAMERA_DISTANCE = 5.0f;

			// Move camera some distance away from the ball
			cameraNode->SetPosition(ballNode->GetPosition() + cameraNode->GetRotation() * Vector3::BACK * CAMERA_DISTANCE);
			showInstructions = true;
		}
	}

	instructionsText_->SetVisible(showInstructions);
}

Controls MyApp::sample_input()
{
	auto ui = GetSubsystem<UI>();
	auto input = GetSubsystem<Input>();

	Controls controls;

	// Copy mouse yaw
	controls.yaw_ = yaw_;

	// Only apply WASD controls if there is no focused UI element
	if (!ui->GetFocusElement())
	{
		controls.Set(CTRL_FORWARD, input->GetKeyDown(KEY_W));
		controls.Set(CTRL_BACK, input->GetKeyDown(KEY_S));
		controls.Set(CTRL_LEFT, input->GetKeyDown(KEY_A));
		controls.Set(CTRL_RIGHT, input->GetKeyDown(KEY_D));
	}

	return controls;
}

void MyApp::apply_input(Node* ballNode, const Controls& controls)
{
	// Torque is relative to the forward vector
	Quaternion rotation(0.0f, controls.yaw_, 0.0f);

#define CSP_TEST_USE_PHYSICS // used for testing to make sure problems aren't related to the physics
#ifdef CSP_TEST_USE_PHYSICS
	auto* body = ballNode->GetComponent<RigidBody>();

	const float MOVE_TORQUE = 3.0f;

	auto change_func = [&](Vector3 force) {
		//#define CSP_TEST_USE_VELOCITY
#ifdef CSP_TEST_USE_VELOCITY
		body->ApplyForce(force);
#else
		body->ApplyTorque(force);
#endif
	};

	// Movement torque is applied before each simulation step, which happen at 60 FPS. This makes the simulation
	// independent from rendering framerate. We could also apply forces (which would enable in-air control),
	// but want to emphasize that it's a ball which should only control its motion by rolling along the ground
	if (controls.buttons_ & CTRL_FORWARD)
		change_func(rotation * Vector3::RIGHT * MOVE_TORQUE);
	if (controls.buttons_ & CTRL_BACK)
		change_func(rotation * Vector3::LEFT * MOVE_TORQUE);
	if (controls.buttons_ & CTRL_LEFT)
		change_func(rotation * Vector3::FORWARD * MOVE_TORQUE);
	if (controls.buttons_ & CTRL_RIGHT)
		change_func(rotation * Vector3::BACK * MOVE_TORQUE);
#else
	const float move_distance = 2.f / scene->GetComponent<PhysicsWorld>()->GetFps();

	// Movement torque is applied before each simulation step, which happen at 60 FPS. This makes the simulation
	// independent from rendering framerate. We could also apply forces (which would enable in-air control),
	// but want to emphasize that it's a ball which should only control its motion by rolling along the ground
	if (controls.buttons_ & CTRL_FORWARD)
		ballNode->SetPosition(ballNode->GetPosition() + Vector3::RIGHT * move_distance);
	if (controls.buttons_ & CTRL_BACK)
		ballNode->SetPosition(ballNode->GetPosition() + Vector3::LEFT * move_distance);
	if (controls.buttons_ & CTRL_LEFT)
		ballNode->SetPosition(ballNode->GetPosition() + Vector3::FORWARD * move_distance);
	if (controls.buttons_ & CTRL_RIGHT)
		ballNode->SetPosition(ballNode->GetPosition() + Vector3::BACK * move_distance);
#endif
}

void MyApp::apply_input(Connection* connection, const Controls& controls)
{
	auto ballNode = serverObjects_[connection];
	if (!ballNode)
		return;

	apply_input(ballNode, controls);
}

void MyApp::HandleSceneUpdate(StringHash eventType, VariantMap & eventData)
{
	// Move the camera by touch, if the camera node is initialized by descendant sample class
	if (cameraNode)
	{
		auto input = GetSubsystem<Input>();
		for (unsigned i = 0; i < input->GetNumTouches(); ++i)
		{
			auto state = input->GetTouch(i);
			if (!state->touchedElement_)    // Touch on empty space
			{
				if (state->delta_.x_ || state->delta_.y_)
				{
					auto camera = cameraNode->GetComponent<Camera>();
					if (!camera)
						return;

					auto graphics = GetSubsystem<Graphics>();
					yaw_ += TOUCH_SENSITIVITY * camera->GetFov() / graphics->GetHeight() * state->delta_.x_;
					pitch_ += TOUCH_SENSITIVITY * camera->GetFov() / graphics->GetHeight() * state->delta_.y_;

					// Construct new orientation for the camera scene node from yaw and pitch; roll is fixed to zero
					cameraNode->SetRotation({ pitch_, yaw_, 0.0f });
				}
				else
				{
					// Move the mouse to the touch position
					if (input->IsMouseVisible())
						input->SetMousePosition(state->position_);
				}
			}
		}
	}
}

void MyApp::HandlePhysicsPreStep(StringHash eventType, VariantMap & eventData)
{
	// This function is different on the client and server. The client collects controls (WASD controls + yaw angle)
	// and sets them to its server connection object, so that they will be sent to the server automatically at a
	// fixed rate, by default 30 FPS. The server will actually apply the controls (authoritative simulation.)
	auto network = GetSubsystem<Network>();
	auto serverConnection = network->GetServerConnection();

	// Client: collect controls
	if (serverConnection)
	{
		auto csp = scene->GetComponent<CSP_Client>();

		if (csp->prediction_controls != nullptr)
		{
			URHO3D_LOGDEBUG("PhysicsPreStep predict");

			if (clientObjectID_) {
				auto ballNode = scene->GetNode(clientObjectID_);
				if (ballNode != nullptr)
					apply_input(ballNode, *csp->prediction_controls);
			}
		}
		else
		{
			URHO3D_LOGDEBUG("PhysicsPreStep sample");

			auto controls = sample_input();

			// predict locally
			if (clientObjectID_) {
				auto ballNode = scene->GetNode(clientObjectID_);
				if (ballNode != nullptr)
					apply_input(ballNode, controls);
			}

			// Set the controls using the CSP system
			csp->add_input(controls);
			//serverConnection->SetControls(controls);

			// In case the server wants to do position-based interest management using the NetworkPriority components, we should also
			// tell it our observer (camera) position. In this sample it is not in use, but eg. the NinjaSnowWar game uses it
			serverConnection->SetPosition(cameraNode->GetPosition());
		}
	}
	//Server: apply controls to client objects
	else if (network->IsServerRunning()) {
		URHO3D_LOGDEBUG("apply clients' controls");
		auto csp = scene->GetComponent<CSP_Server>();

		const auto& connections = network->GetClientConnections();
		for (const auto& connection : connections)
		{
			if (csp->client_inputs[connection].empty())
				continue;

			auto& controls = csp->client_inputs[connection].front();
			apply_input(connection, controls);
			csp->client_input_IDs[connection] = controls.extraData_["id"].GetUInt();
			csp->client_inputs[connection].pop();
		}
	}
}

void MyApp::HandlePostUpdate(StringHash eventType, VariantMap & eventData)
{
	// We only rotate the camera according to mouse movement since last frame, so do not need the time step
	MoveCamera();
}

void MyApp::HandleConnect(StringHash eventType, VariantMap & eventData)
{
	auto network = GetSubsystem<Network>();
	String address = textEdit_->GetText().Trimmed();
	if (address.Empty())
		address = "localhost"; // Use localhost to connect if nothing else specified

	// setup client side prediction
	auto csp = scene->CreateComponent<CSP_Client>(LOCAL);
	csp->timestep = 1.f / scene->GetComponent<PhysicsWorld>()->GetFps();

	// Connect to server, specify scene to use as a client for replication
	clientObjectID_ = 0; // Reset own object ID from possible previous connection
	network->Connect(address, SERVER_PORT, scene);

	UpdateButtons();
}

void MyApp::HandleDisconnect(StringHash eventType, VariantMap & eventData)
{
	auto network = GetSubsystem<Network>();
	auto serverConnection = network->GetServerConnection();
	// If we were connected to server, disconnect. Or if we were running a server, stop it. In both cases clear the
	// scene of all replicated content, but let the local nodes & components (the static world + camera) stay
	if (serverConnection)
	{
		serverConnection->Disconnect();
		scene->Clear(true, false);
		clientObjectID_ = 0;
	}
	// Or if we were running a server, stop it
	else if (network->IsServerRunning())
	{
		network->StopServer();
		scene->Clear(true, false);
	}

	UpdateButtons();
}

void MyApp::HandleStartServer(StringHash eventType, VariantMap & eventData)
{
	auto network = GetSubsystem<Network>();
	network->StartServer(SERVER_PORT);

	// setup client side prediction
	auto csp = scene->CreateComponent<CSP_Server>(LOCAL);
	csp->timestep = 1.f / scene->GetComponent<PhysicsWorld>()->GetFps();
#ifdef CSP_DEBUG
	csp->updateInterval_ = 1.f;//debugging
#endif

	// client input
	csp->apply_client_input = [&](Controls input, float timestep, Connection* connection) {
		apply_input(connection, input);
	};

	UpdateButtons();
}

void MyApp::HandleConnectionStatus(StringHash eventType, VariantMap & eventData)
{
	UpdateButtons();
}

void MyApp::HandleClientConnected(StringHash eventType, VariantMap & eventData)
{
	using namespace ClientConnected;

	// When a client connects, assign to scene to begin scene replication
	auto newConnection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
	newConnection->SetScene(scene);

	// Then create a controllable object for that client
	auto newObject = CreateControllableObject();
	serverObjects_[newConnection] = newObject;

	// Finally send the object's node ID using a remote event
	VariantMap remoteEventData;
	remoteEventData[P_ID] = newObject->GetID();
	newConnection->SendRemoteEvent(E_CLIENTOBJECTID, true, remoteEventData);
}

void MyApp::HandleClientDisconnected(StringHash eventType, VariantMap & eventData)
{
	using namespace ClientConnected;

	// When a client disconnects, remove the controlled object
	auto connection = static_cast<Connection*>(eventData[P_CONNECTION].GetPtr());
	auto object = serverObjects_[connection];
	if (object)
		object->Remove();

	serverObjects_.Erase(connection);
}

void MyApp::HandleClientObjectID(StringHash eventType, VariantMap & eventData)
{
	clientObjectID_ = eventData[P_ID].GetUInt();
}

//
// HandleKeyDown
//
void MyApp::HandleKeyDown(StringHash eventType, VariantMap& eventData)
{
	using namespace KeyDown;

	int key = eventData[P_KEY].GetInt();
	if (key == KEY_ESCAPE && GetPlatform() != "Web")
		engine_->Exit();

	// Toggle console
	if (key == KEY_F1)
		GetSubsystem<Console>()->Toggle();

	// Toggle debug HUD
	if (key == KEY_F2)
		GetSubsystem<DebugHud>()->Toggle(DEBUGHUD_SHOW_STATS);
}