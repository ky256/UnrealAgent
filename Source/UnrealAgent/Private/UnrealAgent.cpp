// Copyright KuoYu. All Rights Reserved.

#include "UnrealAgent.h"
#include "Server/UATcpServer.h"
#include "Settings/UASettings.h"
#include "UALogCapture.h"
#include "UAEventCache.h"
#include "LevelEditor.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FUnrealAgentModule"

DEFINE_LOG_CATEGORY(LogUnrealAgent);

void FUnrealAgentModule::StartupModule()
{
	UE_LOG(LogUnrealAgent, Log, TEXT("UnrealAgent module starting up..."));

	// 初始化日志截获系统
	UALogCapture::Get().Initialize();

	// 初始化事件缓存系统
	UAEventCache::Get().Initialize();

	// Register menu entry
	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUnrealAgentModule::RegisterMenuEntry)
	);

	// Create TCP server
	TcpServer = MakeShared<UATcpServer>();

	// Auto-start if configured
	const UUASettings* Settings = GetDefault<UUASettings>();
	if (Settings->bAutoStart)
	{
		if (TcpServer->Start(Settings->BindAddress, Settings->ServerPort))
		{
			UE_LOG(LogUnrealAgent, Log, TEXT("UnrealAgent TCP server started on %s:%d"),
				*Settings->BindAddress, Settings->ServerPort);
		}
		else
		{
			UE_LOG(LogUnrealAgent, Error, TEXT("Failed to start UnrealAgent TCP server on %s:%d"),
				*Settings->BindAddress, Settings->ServerPort);
		}
	}
}

void FUnrealAgentModule::ShutdownModule()
{
	UE_LOG(LogUnrealAgent, Log, TEXT("UnrealAgent module shutting down..."));

	// 关闭事件缓存系统
	UAEventCache::Get().Shutdown();

	// 关闭日志截获系统
	UALogCapture::Get().Shutdown();

	if (TcpServer.IsValid())
	{
		TcpServer->Stop();
		TcpServer.Reset();
	}
}

void FUnrealAgentModule::RegisterMenuEntry()
{
	UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("MainFrame.MainMenu");
	FToolMenuSection& Section = MainMenu->FindOrAddSection("UnrealAgentMenu");

	Section.AddSubMenu(
		"UnrealAgentMenu",
		FText::FromString(TEXT("UnrealAgent")),
		FText::FromString(TEXT("AI Agent Control Interface")),
		FNewMenuDelegate::CreateRaw(this, &FUnrealAgentModule::GenerateAgentMenu),
		false,
		FSlateIcon()
	);
}

void FUnrealAgentModule::GenerateAgentMenu(FMenuBuilder& MenuBuilder)
{
	const bool bRunning = TcpServer.IsValid() && TcpServer->IsRunning();
	const FString StatusText = bRunning
		? FString::Printf(TEXT("Server: Running (port %d)"), GetDefault<UUASettings>()->ServerPort)
		: TEXT("Server: Stopped");

	// Status display (non-interactive)
	MenuBuilder.AddMenuEntry(
		FText::FromString(StatusText),
		FText::FromString(TEXT("Current server status")),
		FSlateIcon(),
		FUIAction(),
		NAME_None,
		EUserInterfaceActionType::None
	);

	MenuBuilder.AddSeparator();

	// Toggle server
	MenuBuilder.AddMenuEntry(
		FText::FromString(bRunning ? TEXT("Stop Server") : TEXT("Start Server")),
		FText::FromString(bRunning ? TEXT("Stop the TCP server") : TEXT("Start the TCP server")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUnrealAgentModule::ToggleServer))
	);
}

void FUnrealAgentModule::ToggleServer()
{
	if (!TcpServer.IsValid())
	{
		return;
	}

	if (TcpServer->IsRunning())
	{
		TcpServer->Stop();
		UE_LOG(LogUnrealAgent, Log, TEXT("UnrealAgent TCP server stopped"));
	}
	else
	{
		const UUASettings* Settings = GetDefault<UUASettings>();
		if (TcpServer->Start(Settings->BindAddress, Settings->ServerPort))
		{
			UE_LOG(LogUnrealAgent, Log, TEXT("UnrealAgent TCP server started on %s:%d"),
				*Settings->BindAddress, Settings->ServerPort);
		}
		else
		{
			UE_LOG(LogUnrealAgent, Error, TEXT("Failed to start UnrealAgent TCP server"));
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUnrealAgentModule, UnrealAgent)
