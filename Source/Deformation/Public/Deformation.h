#pragma once

#include "Modules/ModuleManager.h"

class FDeformationModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
