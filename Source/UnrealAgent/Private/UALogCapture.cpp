// Copyright KuoYu. All Rights Reserved.

#include "UALogCapture.h"
#include "UnrealAgent.h"
#include "Misc/OutputDeviceRedirector.h"

UALogCapture& UALogCapture::Get()
{
	static UALogCapture Instance;
	return Instance;
}

UALogCapture::~UALogCapture()
{
	Shutdown();
}

void UALogCapture::Initialize()
{
	if (bInitialized)
	{
		return;
	}

	GLog->AddOutputDevice(this);
	bInitialized = true;
	UE_LOG(LogUnrealAgent, Log, TEXT("UALogCapture initialized — capturing output log"));
}

void UALogCapture::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
	bInitialized = false;

	FScopeLock ScopeLock(&Lock);
	Buffer.Empty();
}

void UALogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock ScopeLock(&Lock);

	FUALogEntry Entry;
	Entry.Text = V;
	Entry.Category = Category.ToString();
	Entry.Severity = VerbosityToString(Verbosity);
	Entry.Timestamp = FDateTime::Now();

	// 环形缓冲区：满了则移除最旧的
	if (Buffer.Num() >= MaxBufferSize)
	{
		Buffer.RemoveAt(0);
	}
	Buffer.Add(MoveTemp(Entry));
}

TArray<FUALogEntry> UALogCapture::GetRecent(int32 Count, const FString& CategoryFilter) const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FUALogEntry> Result;
	Count = FMath::Clamp(Count, 1, Buffer.Num());

	// 从尾部向前遍历
	for (int32 i = Buffer.Num() - 1; i >= 0 && Result.Num() < Count; --i)
	{
		const FUALogEntry& Entry = Buffer[i];

		// 过滤类别
		if (!CategoryFilter.IsEmpty() && !Entry.Category.Contains(CategoryFilter))
		{
			continue;
		}

		Result.Add(Entry);
	}

	// 反转，使时间顺序从旧到新
	Algo::Reverse(Result);
	return Result;
}

TArray<FUALogEntry> UALogCapture::GetSince(const FDateTime& Since, const FString& CategoryFilter) const
{
	FScopeLock ScopeLock(&Lock);

	TArray<FUALogEntry> Result;

	for (const FUALogEntry& Entry : Buffer)
	{
		if (Entry.Timestamp < Since)
		{
			continue;
		}

		if (!CategoryFilter.IsEmpty() && !Entry.Category.Contains(CategoryFilter))
		{
			continue;
		}

		Result.Add(Entry);
	}

	return Result;
}

void UALogCapture::Clear()
{
	FScopeLock ScopeLock(&Lock);
	Buffer.Empty();
}

FString UALogCapture::VerbosityToString(ELogVerbosity::Type Verbosity)
{
	switch (Verbosity)
	{
	case ELogVerbosity::Fatal:       return TEXT("Fatal");
	case ELogVerbosity::Error:       return TEXT("Error");
	case ELogVerbosity::Warning:     return TEXT("Warning");
	case ELogVerbosity::Display:     return TEXT("Display");
	case ELogVerbosity::Log:         return TEXT("Log");
	case ELogVerbosity::Verbose:     return TEXT("Verbose");
	case ELogVerbosity::VeryVerbose: return TEXT("VeryVerbose");
	default:                         return TEXT("Unknown");
	}
}
