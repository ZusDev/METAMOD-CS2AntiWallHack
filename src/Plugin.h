#pragma once
#include <ISmmPlugin.h>
#include <engine2/isource2server.h>
#include <engine2/inetworkserverservice.h>
#include <engine2/networkgameserverbase.h>
#include <engine2/igameresourceservice.h>
#include <engine2/ivengineserver2.h>
#include <entity2/entitysystem.h>
#include <gametrace.h>
#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <future>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cerrno>
#include <sys/resource.h>
#include "Helpers/VisibilityRules.h"
#include "Visibility/CornerPrediction.h"
#include "Visibility/MVisibility.h"
#include "Helpers/Targeting.h"
#include "Visibility/VisibilityMap.h"
#include "Networking/TransmitInfo.h"
#include "Helpers/Memory.h"
#include "Helpers/ModuleAnchor.h"
#include "Helpers/WorkerAffinity.h"
#include "Helpers/AttachmentTraversal.h"
#include "Helpers/Jsonc.h"
#include "Config/Config.h"
#include "Config/Gamedata.h"
#include "Updates/ReleaseUpdater.h"
#include "Smoke/SmokeMemory.h"
#include <igameevents.h>

#include "Helpers/Entities.h"
#include "Config/Offsets.h"
#include "Models/PlayerSnapshot.h"
#include "Managers/SightFilter.h"

PLUGIN_GLOBALVARS();
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace awh;

class AntiWallHack final : public ISmmPlugin, public IMetamodListener
{
    using TraceFunction = bool ( * )( void*, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace* );
    using TraceHook = KHook::Function<bool, void*, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace*>;
    ISource2GameEntities* gameEntities = nullptr;
    ISource2Server* serverDll = nullptr;
    using FrameHook = KHook::Virtual<ISource2Server, void, bool, bool, bool>;
    std::unique_ptr<FrameHook> frameHook;
    INetworkServerService* network = nullptr;
    void* resources = nullptr;
    CGameEntitySystem* entitySystem = nullptr;
    Offsets offsets;
    Settings settings;
    fs::path configPath, gamePath;
    std::shared_ptr<std::atomic<bool>> updateCancel = std::make_shared<std::atomic<bool>>( false );
    std::future<updates::Result> updatePending;
    std::chrono::steady_clock::time_point updateNextCheck{};
    bool updateInstalled = false;
    VisibilityMap visibility;
    std::string visMapName;
    std::string visLoadingMap;
    uint64_t visGeneration = 0, visLoadingGeneration = 0;
    fs::file_time_type visObservedTime{};
    uintmax_t visObservedSize = 0;
    bool visObserved = false, visObservedExists = false;
    std::mutex debugMutex;
    unsigned long long debugSequence = 0;
    std::atomic<bool> debugTraceSeen{ false };
    bool debugTransmitSeen = false, debugTransmitReady = false, debugFrameSeen = false;
    std::shared_ptr<std::atomic<bool>> visCancel;
    std::future<VisibilityMap> visPending;

    std::chrono::steady_clock::time_point visNextPoll{};
    std::chrono::steady_clock::time_point visNextNotice{};
    std::string visStatus = "waiting for map physics before VIS validation";

    class EventHook final : public KHook::Virtual<IGameEventManager2, bool, IGameEvent*, bool>
    {
        using Base = KHook::Virtual<IGameEventManager2, bool, IGameEvent*, bool>;

      public:
        using Base::Base;

        bool Installed( void** vtable, int index )
        {
            std::lock_guard guard( this->_hooks_stored );
            return this->_addr_hook_ids.count( vtable + index ) != 0;
        }
    };

    std::unique_ptr<EventHook> eventHook;
    IGameEventManager2* eventManager = nullptr;
    uintptr_t eventManagerAddress = 0;
    bool eventHooked = false, smokeAvailable = false, smokeCaptured = false;
    int smokeEffectOffset = -1, smokeCellBudget = 262144;
    smoke::Layout smokeLayout;
    std::chrono::steady_clock::time_point smokeNextWarning{};
    float frameTime = 0;
    std::array<std::unique_ptr<smoke::Volume>, 32> smokeStorage;
    std::vector<const smoke::Volume*> smokeVolumes;
    std::vector<uint8_t> smokeFrame = std::vector<uint8_t>( smoke::FrameBytes );
    smoke::Explosions smokeExplosions;
    int entitySystemOffset = 0;
    std::unique_ptr<TraceHook> traceHook;
    TraceFunction traceFunction = nullptr;
    void* traceManager = nullptr;
    using TransmitHook = KHook::Virtual<ISource2GameEntities, void, CCheckTransmitInfo**, int, CBitVec<16384>&, CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int>;
    std::unique_ptr<TransmitHook> transmitHook;
    std::array<Snapshot, 64> players;
    std::array<Identity, 64> identities{};
    int deadlines[64][64]{};
    int preferred[64][64]{};
    std::array<std::vector<CEntityHandle>, 64> hidden;
    std::array<bool, 64> evaluated{};
    PairVisibilityCache pairVisibility;
    int frameTick = -1, traces = 0;
    bool faulted = false, hooked = false, convarChanged = false;
    std::string previousConvar;

    void DebugStage( const char* stage ) noexcept;
    void NotifyMissingVisibility();
    void PollUpdates();
    KHook::Return<void> OnFrame( ISource2Server*, bool, bool, bool );
    Json ReadGamedata( const fs::path& path );
    void LoadSettings();
    void LoadVisibility( const char* name, bool startValidation = true );
    void UpdateVisibility();
    void StopVisibilityWorker() noexcept;
    void Reset();
    void ConfigureConvar();
    void DisableSmoke( const std::string& reason );
    void RemoveEventHook();
    void SetupSmoke( void* moduleAnchor, CSchemaSystemTypeScope* scope, const Json& platform );
    void CaptureSmoke();
    bool IsSmokeHidden( const Snapshot& viewer, const Snapshot& target, Vector eyeLead, int count, int first );
    bool CollectChildren( void* node, Snapshot& snapshot, AttachmentTraversal& seen, int depth );
    bool RoundActive();
    void BeginFrame( int tick, float now );
    void FillSamples( const Snapshot& target, float padding, std::array<Vector, 12>& points );
    void BuildSamples( Snapshot& target );
    void PrepareViewerPrediction( Snapshot& viewer );
    DirectVisibility CheckVisibility( int viewerSlot, int targetSlot );
    void EvaluateViewer( int slot, int tick );
    const std::vector<CEntityHandle>& GetHidden( int slot, int tick );

  public:
    AntiWallHack() = default;

    bool Load( PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late ) override;
    bool Unload( char*, size_t ) override;
    void OnLevelInit( const char* map, const char*, const char*, const char*, bool, bool ) override;
    void OnLevelShutdown() override;
    KHook::Return<bool> OnGameEvent( IGameEventManager2*, IGameEvent* event, bool );
    KHook::Return<bool> OnTrace( void* manager, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace* );
    KHook::Return<void> OnTransmit( ISource2GameEntities*, CCheckTransmitInfo** infos, int count, CBitVec<16384>&, CBitVec<16384>&, const Entity2Networkable_t**, const uint16*,
                                    int );
    const char* GetAuthor() override;
    const char* GetName() override;
    const char* GetDescription() override;
    const char* GetURL() override;
    const char* GetLicense() override;
    const char* GetVersion() override;
    const char* GetDate() override;
    const char* GetLogTag() override;
};
