#pragma once

#include <memory>
#include "ui/IUiController.h"

class ApplicationCore;

// ==============================================================================
// UiFactory
// ------------------------------------------------------------------------------
// Фабрика создания пользовательского интерфейса (FTXUI, CLI, CLI-Light).
// Поддерживает сборку с отключенным FTXUI (-DENABLE_FTXUI=OFF).
// ==============================================================================
class UiFactory {
public:
    static std::unique_ptr<IUiController> Create(UiMode mode, ApplicationCore& core);
};
