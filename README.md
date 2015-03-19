# Urho3D-CSP
Urho3D Client Side Prediction Subsystem

Note that this subsystem isn't polished.

# Instructions
There are few things you need to do to use the subsystem:
- Set the input timestep. Most likely to be the physics simulation FPS.
- Set std::function to a function that applies input locally.
- Set std::function to a function that applies input provided by a client connection.
- Add LOCAL nodes to the CSP system.
- Add input to the CSP system. Note that you need to manually apply it locally, that is the CSP system only uses it for client side prediction and server side custom input message processing for clients (not locally).

Don't forget to register it as an objcet!
```c++
ClientSidePrediction::RegisterObject(context);
```
(and as a subsystem if you want to)

Initialization example:
```c++
clientSidePrediction->timestep = 1.f / physicsWorld->GetFps();
using namespace std::placeholders;
// local input
std::function<void(Controls, float)> local_input_function = std::bind(&Game::apply_local_input, this, _1, _2);
clientSidePrediction->apply_local_input = local_input_function;
// client input
std::function<void(Controls, float, Connection*)> client_input_function = std::bind(&Game::apply_client_input, this, _1, _2, _3);
clientSidePrediction->apply_client_input = client_input_function;
```

Adding node example:
```c++
clientSidePrediction->add_node(playerNode);
```

Adding input example:
```c++
clientSidePrediction->add_input(local_controller->controls);
```

For more detailed you can look at the ClientSidePrediction class header's public members.

# License
MIT
