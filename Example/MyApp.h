#pragma once

#include <Urho3D/Engine/Application.h>
#undef TRANSPARENT

namespace Urho3D {
	class Node;
	class Scene;
	class UIElement;
	class Text;
	class Button;
	class LineEdit;
	class Connection;
	class Controls;
}

using namespace Urho3D;


const float TOUCH_SENSITIVITY = 2.0f;


struct MyApp : Application
{
	MyApp(Context* context);

	SharedPtr<Scene> scene;
    SharedPtr<Node> cameraNode;
	/// Camera yaw angle.
	float yaw_ = 0.f;
	/// Camera pitch angle.
	float pitch_ = 0.f;

	void Setup();
	void Start() override;

protected:
	/// Mapping from client connections to controllable objects.
	HashMap<Connection*, WeakPtr<Node> > serverObjects_;
	/// Button container element.
	SharedPtr<UIElement> buttonContainer_;
	/// Server address line editor element.
	SharedPtr<LineEdit> textEdit_;
	/// Connect button.
	SharedPtr<Button> connectButton_;
	/// Disconnect button.
	SharedPtr<Button> disconnectButton_;
	/// Start server button.
	SharedPtr<Button> startServerButton_;
	/// Instructions text.
	SharedPtr<Text> instructionsText_;
	/// ID of own controllable object (client only.)
	unsigned clientObjectID_;


	void CreateScene();
	void SetupViewport();
	void SubscribeToEvents();
	void CreateUI();

	/// Create a button to the button container.
	Button* CreateButton(const String& text, int width);
	/// Update visibility of buttons according to connection and server status.
	void UpdateButtons();
	/// Create a controllable ball object and return its scene node.
	Node* CreateControllableObject();
	/// Read input and move the camera.
	void MoveCamera();

	Controls sample_input();

	void apply_input(Node* ballNode, const Controls& controls);
	void apply_input(Connection* connection, const Controls& controls);

	/// Handle scene update event to control camera's pitch and yaw for all samples.
	void HandleSceneUpdate(StringHash eventType, VariantMap& eventData);

	/// Handle the physics world pre-step event.
	void HandlePhysicsPreStep(StringHash eventType, VariantMap& eventData);
	/// Handle the logic post-update event.
	void HandlePostUpdate(StringHash eventType, VariantMap& eventData);
	/// Handle pressing the connect button.
	void HandleConnect(StringHash eventType, VariantMap& eventData);
	/// Handle pressing the disconnect button.
	void HandleDisconnect(StringHash eventType, VariantMap& eventData);
	/// Handle pressing the start server button.
	void HandleStartServer(StringHash eventType, VariantMap& eventData);
	/// Handle connection status change (just update the buttons that should be shown.)
	void HandleConnectionStatus(StringHash eventType, VariantMap& eventData);
	/// Handle a client connecting to the server.
	void HandleClientConnected(StringHash eventType, VariantMap& eventData);
	/// Handle a client disconnecting from the server.
	void HandleClientDisconnected(StringHash eventType, VariantMap& eventData);
	/// Handle remote event from server which tells our controlled object node ID.
	void HandleClientObjectID(StringHash eventType, VariantMap& eventData);

	void HandleKeyDown(StringHash eventType, VariantMap& eventData);
};
