//
// Game.h
//

#pragma once

#include "DeviceResources.h"
#include "StepTimer.h"
#include "LogWindow.h"
#include "HAUtils.h"

enum class GameLinkLayout
{
    NORMAL      = 0,
    FLIPPED_X   = 1,
    FLIPPED_Y   = 2,
    FLIPPED_XY  = 3,
    NONE        = UINT8_MAX
};

// A basic game implementation that creates a D3D12 device and
// provides a game loop.
class Game final : public DX::IDeviceNotify
{
public:

    Game() noexcept(false);
    ~Game();

    Game(Game&&) = default;
    Game& operator= (Game&&) = default;

    Game(Game const&) = delete;
    Game& operator= (Game const&) = delete;

    // Initialization and management
    void Initialize(HWND window, int width, int height);

    // Basic game loop
    void Tick();

    // IDeviceNotify
    void OnDeviceLost() override;
    void OnDeviceRestored() override;

    // Messages
    void OnActivated();
    void OnDeactivated();
    void OnSuspending();
    void OnResuming();
	void OnWindowChanged();                             // we know the client rect changed but we don't know what changed
    void OnWindowMoved(LONG cR_x, LONG cR_y);           // pass in origin of client rect
	void OnWindowSizeChanged(LONG width, LONG height);  // pass in new width and height of client rect

    // Menu commands
    void MenuActivateProfile();
    void MenuDeactivateProfile();
    void MenuShowLogWindow(bool toggle);

    // Other methods
    D3D12_RESOURCE_DESC ChooseTexture();
    void SetVideoLayout(GameLinkLayout layout);
    void SetWindowSizeOnChangedProfile();

    void GetBaseSize(__out int& width, __out int& height) noexcept;

    // Properties
    bool shouldRender;

private:

    void Update(DX::StepTimer const& timer);
    void Render();

    void Clear();

    void CreateDeviceDependentResources();
    void CreateWindowSizeDependentResources();

    void SetVertexData(HA::Vertex* v, float wRatio, float hRatio, GameLinkLayout layout);

    // This method must be called when sidebars are added or deleted
    // so the relative vertex boundaries can be updated to stay within
    // the aspect ratio of the gamelink texture. Pass in width and height ratios
    // (i.e. how much of the total width|height the vertex should fill, 1.f being full width|height)
    void UpdateGamelinkVertexData(int width, int height, float wRatio, float hRatio);

    // Device resources.
    std::unique_ptr<DX::DeviceResources>    m_deviceResources;

    // Rendering loop timer.
    DX::StepTimer                           m_timer;

    // Vars to choose whether to display GameLink or not
    const uint32_t m_framesDelay = 60;
    uint32_t m_previousFrameCount;    // Last render frame count
    UINT16 m_previousGameLinkFrameSequence;
    bool m_useGameLink;

    // Background image when GameLink isn't available
    std::vector<uint8_t> m_bgImage;
    uint32_t m_bgImageWidth;
    uint32_t m_bgImageHeight;

    // video texture layout may change depending on gamelink status
    GameLinkLayout m_currentLayout;
    GameLinkLayout m_previousLayout;

    // Input devices.
    std::unique_ptr<DirectX::GamePad>       m_gamePad;
    std::unique_ptr<DirectX::Keyboard>      m_keyboard;

    std::unique_ptr<DirectX::GraphicsMemory> m_graphicsMemory;
    std::unique_ptr<DirectX::DescriptorHeap> m_resourceDescriptors;

    std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
    DirectX::SimpleMath::Vector2 m_fontPos;

    // Direct3D 12 objects for AppleWin video texture
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>    m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>     m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>     m_pipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_indexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource>          m_texture;
    D3D12_VERTEX_BUFFER_VIEW                        m_vertexBufferView;
    D3D12_INDEX_BUFFER_VIEW                         m_indexBufferView;
};
