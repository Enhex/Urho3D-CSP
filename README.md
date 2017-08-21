# Urho3D-CSP
Urho3D Client Side Prediction Subsystem

Note that this subsystem isn't polished.

# Limitations
Currently full game state rewinding isn't provided.
For things like physics to work the whole physics world needs to be rewinded and stepped for each replayed input.
It's also needed for other interactions which aren't directly trigged by input.

# Instructions
There are few things you need to do to use the subsystem:
- Set the input timestep. Most likely to be the physics simulation FPS.
- Set std::function to a function that applies input locally to the client/server.
- Set std::function to a function that applies input provided by a client connection.
- Add LOCAL server-side nodes to the CSP system.
- Add input to the CSP system. Note that you need to manually apply it locally, that is the CSP system only uses it for client side prediction and server side custom input message processing for clients (not locally).

Don't forget to register it as an object!
```c++
ClientSidePrediction::RegisterObject(context);
```
(and as a subsystem if you want to)

Initialization example:
```c++
clientSidePrediction->timestep = 1.f / physicsWorld->GetFps();
// local input
csp->apply_local_input = [&](Controls input, float timestep) {
  apply_input(scene->GetNode(clientObjectID_), input);
};
// client input
csp->apply_client_input = [&](Controls input, float timestep, Connection* connection) {
  apply_input(connection, input);
};
```

Adding node example:
```c++
clientSidePrediction->add_node(playerNode);
```

Adding input example:
```c++
clientSidePrediction->add_input(local_controller->controls);
```

For more detailed you can look at the example project and ClientSidePrediction header.
Use CMake to build the example. It's a [downstream Urho3D project](https://urho3d.github.io/documentation/HEAD/_using_library.html).

# License
MIT
