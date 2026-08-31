// Copyright 2026, 123dx-svg. MIT License.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SmithUESettings.generated.h"

/**
 * SmithUE plugin settings — configurable via Editor → Project Settings → Plugins → SmithUE.
 * Changes take effect after restarting the Unreal Editor.
 */
UCLASS(config=EditorPerProjectUserSettings, defaultconfig, meta=(DisplayName="SmithUE"))
class SMITHUE_API USmithUESettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USmithUESettings();

	/** Port the SmithUE HTTP server binds to on startup.
	 *  Override precedence (highest to lowest): command-line SmithUEhttpport= > SMITHUE_BIND_PORT env var > this setting.
	 *  Valid range: 1024–65535. Default: 13721. Requires Editor restart. */
	UPROPERTY(config, EditAnywhere, Category="Server",
		meta=(ClampMin=1024, ClampMax=65535,
			  ToolTip="HTTP port SmithUE listens on. Overridden by SMITHUE_BIND_PORT env var or -SmithUEhttpport= command line. Requires Editor restart."))
	int32 HttpBindPort = 13721;

	/** If false, the SmithUE HTTP server will not start automatically when the Editor launches.
	 *  Requires Editor restart. */
	UPROPERTY(config, EditAnywhere, Category="Server",
		meta=(ToolTip="Start the SmithUE HTTP server automatically on Editor launch. Requires Editor restart."))
	bool bAutoStartServer = true;

	/** How often (in seconds) SmithUE re-writes the portfile to keep it alive.
	 *  Valid range: 1.0–30.0 seconds. Default: 4.0. Requires Editor restart. */
	UPROPERTY(config, EditAnywhere, Category="Server",
		meta=(ClampMin=1.0f, ClampMax=30.0f,
			  ToolTip="Portfile heartbeat interval in seconds. Requires Editor restart."))
	float PortfileHeartbeatInterval = 4.0f;

	/** If false, SmithUE will not check for plugin updates on Editor startup. */
	UPROPERTY(config, EditAnywhere, Category="Status & Updates",
		meta=(ToolTip="Check for SmithUE plugin updates automatically on Editor startup."))
	bool bCheckForUpdatesOnStartup = true;

	/** If true, SmithUE will check for Node/npm/smithue-cli on Editor startup (non-blocking, ~7s delay). */
	UPROPERTY(config, EditAnywhere, Category="Status & Updates",
		meta=(ToolTip="Detect Node.js / npm / smithue-cli environment on Editor startup. Non-blocking. Result visible in Project Settings → SmithUE."))
	bool bCheckCliOnStartup = true;

	/**
	 * If true, an out-of-date (or missing) smithue-cli is upgraded automatically
	 * after the startup probe, without waiting for a button press.
	 * Runs at most once per Editor session and only when npm is available.
	 * NOTE: this modifies the machine's GLOBAL npm packages. Turn it off on shared
	 * or locked-down machines, or where the CLI version is pinned by policy.
	 * Requires bCheckCliOnStartup.
	 */
	UPROPERTY(config, EditAnywhere, Category="Status & Updates",
		meta=(EditCondition="bCheckCliOnStartup",
			  ToolTip="Automatically run 'npm i -g smithue-cli@latest' on Editor startup when the CLI is missing or outdated. Once per session. Modifies global npm packages."))
	bool bAutoUpgradeCliOnStartup = true;

	/** Guide: visit Pollinations.ai to register and get your free API key for audio generation.
	 *  Image generation (generate_texture) is free and requires no key.
	 *  Audio generation (generate_audio) requires a Pollinations API key (free quota available). */
	UPROPERTY(VisibleAnywhere, Category="AI Generation",
		meta=(DisplayName="Get Started: Pollinations.ai",
			  ToolTip="Visit https://pollinations.ai to create an account and get your free API key. Image generation is free (no key needed). Audio generation requires a key."))
	FString PollinationsGuideUrl = TEXT("https://pollinations.ai  (Image: free, no key | Audio: free key required)");

	/** Default AI image generation endpoint. Leave empty to use Pollinations.ai (free, no key needed). */
	UPROPERTY(config, EditAnywhere, Category="AI Generation",
		meta=(DisplayName="Default Image Endpoint",
			  ToolTip="AI image generation API URL. Leave empty to use Pollinations.ai (free, no registration). For DALL-E: https://api.openai.com/v1/images/generations"))
	FString DefaultImageEndpoint;

	/** Default API key for the image endpoint. Not needed for Pollinations.ai image generation. */
	UPROPERTY(config, EditAnywhere, Category="AI Generation",
		meta=(DisplayName="Default Image API Key",
			  ToolTip="API key for non-Pollinations image endpoints (DALL-E, Imagen, etc). Pollinations image generation is free and needs no key."))
	FString DefaultApiKey;

	/** Default model name (e.g. flux, dall-e-3). Leave empty for provider default. */
	UPROPERTY(config, EditAnywhere, Category="AI Generation", meta=(DisplayName="Default Model"))
	FString DefaultModel;

	/** Pollinations.ai API key for audio generation (gen.pollinations.ai/audio). Get one free at pollinations.ai */
	UPROPERTY(config, EditAnywhere, Category="AI Generation",
		meta=(DisplayName="Pollinations Audio API Key",
			  ToolTip="Required for generate_audio. Get a free key at https://pollinations.ai. Leave empty if providing api_key directly in the tool call."))
	FString DefaultAudioApiKey;

	static const USmithUESettings* Get()
	{
		return GetDefault<USmithUESettings>();
	}

	// UDeveloperSettings interface
	// GetContainerName must return "Project" to appear under Project Settings → Plugins.
	// Default for EditorPerProjectUserSettings is "Editor" (→ Editor Preferences), not "Project".
	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override  { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override   { return TEXT("SmithUE"); }
};
