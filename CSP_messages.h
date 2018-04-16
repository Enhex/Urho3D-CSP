#pragma once

namespace Urho3D
{
	/* Client Side Prediction  Message IDs */
	/* Client -> server */
	// Custom input message to add update ID and be in sync with the update rate
	constexpr int MSG_CSP_INPUT = 32;
	/* Server -> client */
	// Sends a complete snapshot of the world
	constexpr int MSG_CSP_STATE = 33;
}
