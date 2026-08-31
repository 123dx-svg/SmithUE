// Copyright 2026, 123dx-svg. MIT License.

#include "SmithUESettingsCustomization.h"

#include "SmithUESettings.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Utils/SmithUECliChecker.h"
#include "Utils/SmithUEUpdateChecker.h"
#include "Widgets/Input/SButton.h"

TSharedRef<IDetailCustomization> FSmithUESettingsCustomization::MakeInstance()
{
	return MakeShareable(new FSmithUESettingsCustomization);
}

void FSmithUESettingsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Trigger a CLI check on first panel open if no check has run yet
	if (FSmithUECliChecker::GetLastCheckTime().GetTicks() == 0
		&& !FSmithUECliChecker::IsCheckInFlight())
	{
		FSmithUECliChecker::CheckCliEnvironment();
	}

	IDetailCategoryBuilder& AICat = DetailBuilder.EditCategory(TEXT("AI Generation"));

	DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USmithUESettings, PollinationsGuideUrl)));

	AICat.AddCustomRow(FText::FromString(TEXT("Pollinations Links")))
	.WholeRowContent()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Image Generation (free, no key): ")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("https://pollinations.ai")))
				.ToolTipText(FText::FromString(TEXT("Open Pollinations.ai in browser - image generation is free, no registration needed")))
				.OnNavigate_Lambda([](){ FPlatformProcess::LaunchURL(TEXT("https://pollinations.ai"), nullptr, nullptr); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Audio Generation (free key required): ")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("https://pollinations.ai - Get Free API Key")))
				.ToolTipText(FText::FromString(TEXT("Open Pollinations.ai to register and get your free API key for audio generation")))
				.OnNavigate_Lambda([](){ FPlatformProcess::LaunchURL(TEXT("https://pollinations.ai"), nullptr, nullptr); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("API Documentation: ")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("https://gen.pollinations.ai/docs")))
				.ToolTipText(FText::FromString(TEXT("Open Pollinations API documentation - see available models, parameters, and endpoints")))
				.OnNavigate_Lambda([](){ FPlatformProcess::LaunchURL(TEXT("https://gen.pollinations.ai/docs"), nullptr, nullptr); })
			]
		]
	];

	// ---- Status & Updates category ----
	IDetailCategoryBuilder& StatusCat = DetailBuilder.EditCategory(TEXT("Status & Updates"));

	StatusCat.AddCustomRow(FText::FromString(TEXT("Plugin Update")))
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		// Row 1: plugin update status text + Releases hyperlink
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([]() -> FText {
					if (FSmithUEUpdateChecker::IsUpdateAvailable())
					{
						return FText::FromString(FString::Printf(
							TEXT("SmithUE \u6709\u65b0\u7248\u672c\u53ef\u7528 (\u5f53\u524d v%s)  "),
							*FSmithUEUpdateChecker::GetCurrentVersion()));
					}
					return FText::FromString(FString::Printf(
						TEXT("\u63d2\u4ef6\u5df2\u662f\u6700\u65b0 (v%s)  "),
						*FSmithUEUpdateChecker::GetCurrentVersion()));
				})
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("GitHub Releases")))
				.ToolTipText(FText::FromString(TEXT("Open SmithUE releases page")))
				.OnNavigate_Lambda([](){
					FPlatformProcess::LaunchURL(
						TEXT("https://github.com/123dx-svg/SmithUE/releases"),
						nullptr, nullptr);
				})
			]
		]

		// Row 2: "Restart to Update" button -- visible only when update available
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Restart to Update")))
				.ToolTipText(FText::FromString(TEXT("Pull latest changes and restart editor")))
				.Visibility_Lambda([]() -> EVisibility {
					return FSmithUEUpdateChecker::IsUpdateAvailable()
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.OnClicked_Lambda([]() -> FReply {
					FSmithUEUpdateChecker::ExecuteUpdate();
					return FReply::Handled();
				})
			]
		]

		// Row 3: CLI environment status (live via pull)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
		[
			SNew(STextBlock)
			.Text_Lambda([]() -> FText {
				if (!FSmithUECliChecker::GetLastCheckTime().GetTicks())
				{
					return FText::FromString(TEXT("CLI \u73af\u5883: \u672a\u68c0\u6d4b \u2014 \u70b9\u51fb\u300c\u91cd\u65b0\u68c0\u6d4b\u300d"));
				}
				const FString StateStr = []{
					switch (FSmithUECliChecker::GetState())
					{
						case ECliState::NoNode:       return TEXT("\u26a0 Node.js \u672a\u627e\u5230");
						case ECliState::NotInstalled: return TEXT("\u26a0 smithue-cli \u672a\u5b89\u88c5");
						case ECliState::Outdated:     return TEXT("\u2191 smithue-cli \u9700\u8981\u5347\u7ea7");
						case ECliState::Ready:        return TEXT("\u2713 Ready");
						default:                      return TEXT("Unknown");
					}
				}();
				const FDateTime T = FSmithUECliChecker::GetLastCheckTime();
				// Show what the registry says so it is obvious whether the machine is
				// behind the live "latest" or only behind the plugin's floor.
				const FString Latest = FSmithUECliChecker::GetLatestCliVersion();
				const FString LatestStr = Latest.IsEmpty()
					? FString(TEXT("(npm \u672a\u67e5\u8be2\u5230)"))
					: Latest;
				return FText::FromString(FString::Printf(
					TEXT("CLI \u73af\u5883: node v%s \u00b7 npm %s \u00b7 smithue-cli %s (npm latest %s) \u00b7 %s \u00b7 \u4e0a\u6b21\u68c0\u6d4b %02d:%02d:%02d"),
					*FSmithUECliChecker::GetNodeVersion(),
					*FSmithUECliChecker::GetNpmVersion(),
					*FSmithUECliChecker::GetCliVersion(),
					*LatestStr,
					*StateStr,
					T.GetHour(), T.GetMinute(), T.GetSecond()));
			})
		]

		// Row 4: Re-check + Install/Update buttons
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("\u91cd\u65b0\u68c0\u6d4b / Re-check")))
				.IsEnabled_Lambda([]() -> bool {
					return !FSmithUECliChecker::IsCheckInFlight()
						&& !FSmithUECliChecker::IsInstallInFlight();
				})
				.OnClicked_Lambda([]() -> FReply {
					FSmithUECliChecker::CheckCliEnvironment();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
			[
				SNew(SButton)
				.Text_Lambda([]() -> FText {
					// During an install the button becomes a Cancel control.
					if (FSmithUECliChecker::IsInstallInFlight())
					{
						return FText::FromString(TEXT("\u53d6\u6d88")); // 取消
					}
					// While a check is running, show a non-actionable progress label.
					if (FSmithUECliChecker::IsCheckInFlight())
					{
						return FText::FromString(TEXT("\u5904\u7406\u4e2d\u2026")); // 处理中…
					}
					switch (FSmithUECliChecker::GetState())
					{
						case ECliState::NotInstalled: return FText::FromString(TEXT("\u5b89\u88c5 smithue-cli"));        // 安装 smithue-cli
						case ECliState::Outdated:     return FText::FromString(TEXT("\u5347\u7ea7\u5230\u6700\u65b0"));   // 升级到最新
						case ECliState::Ready:        return FText::FromString(TEXT("\u5f3a\u5236\u91cd\u88c5"));          // 强制重装
						case ECliState::NoNode:       return FText::FromString(TEXT("\u9700\u5148\u5b89\u88c5 Node.js")); // 需先安装 Node.js
						default:                      return FText::FromString(TEXT("smithue-cli"));
					}
				})
				.ToolTipText_Lambda([]() -> FText {
					switch (FSmithUECliChecker::GetState())
					{
						case ECliState::Ready:  return FText::FromString(TEXT("Already up to date. Click to force-reinstall (npm i -g smithue-cli@latest), e.g. to repair a broken install or re-deploy the SKILL."));
						case ECliState::NoNode: return FText::FromString(TEXT("Install Node.js first (see the link below)"));
						default:                return FText::FromString(TEXT("Run: npm i -g smithue-cli@latest"));
					}
				})
				.IsEnabled_Lambda([]() -> bool {
					// While installing, the button is the Cancel control and is always clickable.
					if (FSmithUECliChecker::IsInstallInFlight()) { return true; }
					// Actionable in every state except "no Node at all" -- a healthy
					// machine must still be able to force a repair/reinstall.
					return FSmithUECliChecker::GetState() != ECliState::NoNode
						&& !FSmithUECliChecker::IsCheckInFlight();
				})
				.OnClicked_Lambda([]() -> FReply {
					if (FSmithUECliChecker::IsInstallInFlight())
					{
						FSmithUECliChecker::CancelCliInstall();
					}
					else
					{
						FSmithUECliChecker::ExecuteCliInstall();
					}
					return FReply::Handled();
				})
			]
		]

		// Row 5: SKILL deployment freshness (live via pull)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)
		[
			SNew(STextBlock)
			.Text_Lambda([]() -> FText {
				if (!FSmithUECliChecker::GetLastCheckTime().GetTicks())
				{
					return FText::FromString(TEXT("SKILL: \u672a\u68c0\u6d4b")); // 未检测
				}
				switch (FSmithUECliChecker::GetSkillState())
				{
					case ESkillState::NotDeployed: return FText::FromString(TEXT("SKILL: \u26a0 \u672a\u90e8\u7f72")); // ⚠ 未部署
					case ESkillState::Stale:       return FText::FromString(TEXT("SKILL: \u2191 \u5df2\u8fc7\u671f\uff08\u4e0e\u5df2\u88c5 CLI \u4e0d\u4e00\u81f4\uff09")); // ↑ 已过期（与已装 CLI 不一致）
					case ESkillState::Synced:      return FText::FromString(TEXT("SKILL: \u2713 \u5df2\u540c\u6b65")); // ✓ 已同步
					default:                       return FText::FromString(TEXT("SKILL: \u672a\u77e5\uff08CLI \u672a\u5b89\u88c5\uff09")); // 未知（CLI 未安装）
				}
			})
		]

		// Row 6: "重装 SKILL" button -- visible only when SKILL is NotDeployed or Stale
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("\u91cd\u88c5 SKILL"))) // 重装 SKILL
				.ToolTipText(FText::FromString(TEXT("\u5c06\u5df2\u5b89\u88c5 smithue-cli \u81ea\u5e26\u7684 SKILL.md \u590d\u5236\u5230\u672c\u5730 agent \u6280\u80fd\u76ee\u5f55\u3002\u60f3\u8981\u6700\u65b0\u7248 SKILL\uff0c\u8bf7\u5148\u5347\u7ea7 CLI\u3002"))) // 将已安装 smithue-cli 自带的 SKILL.md 复制到本地 agent 技能目录。想要最新版 SKILL，请先升级 CLI。
				.Visibility_Lambda([]() -> EVisibility {
					if (!FSmithUECliChecker::GetLastCheckTime().GetTicks()) { return EVisibility::Collapsed; }
					const ESkillState S = FSmithUECliChecker::GetSkillState();
					return (S == ESkillState::NotDeployed || S == ESkillState::Stale)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.IsEnabled_Lambda([]() -> bool {
					return !FSmithUECliChecker::IsCheckInFlight()
						&& !FSmithUECliChecker::IsInstallInFlight();
				})
				.OnClicked_Lambda([]() -> FReply {
					FSmithUECliChecker::ReinstallSkill();
					return FReply::Handled();
				})
			]
		]

		// Row 7: nodejs.org link -- visible only when Node is missing
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SHyperlink)
				.Text(FText::FromString(TEXT("\u5b89\u88c5 Node.js (nodejs.org)")))
				.ToolTipText(FText::FromString(TEXT("Download and install Node.js (required for npm and smithue-cli)")))
				.Visibility_Lambda([]() -> EVisibility {
					return (FSmithUECliChecker::GetState() == ECliState::NoNode
							&& FSmithUECliChecker::GetLastCheckTime().GetTicks() != 0)
						? EVisibility::Visible
						: EVisibility::Collapsed;
				})
				.OnNavigate_Lambda([](){
					FPlatformProcess::LaunchURL(TEXT("https://nodejs.org"), nullptr, nullptr);
				})
			]
		]
	];
}
