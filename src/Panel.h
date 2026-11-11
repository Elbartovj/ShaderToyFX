// ShaderToyFX - fenêtre "Shader Library" (Win32 + Direct3D 11 + Dear ImGui, thread dédié)
#pragma once
#include "Instance.h"

namespace Panel
{
// Ouvre (ou ramène au premier plan) la fenêtre, ciblant l'instance donnée
void Open( const stfx::SharedPtr& target );
// À appeler quand une instance disparaît : ferme la fenêtre s'il n'en reste aucune
void OnInstanceRemoved();
}//namespace Panel
