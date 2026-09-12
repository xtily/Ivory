#pragma once

/* =============================================================
/*                       theo's offsets                         
/*                  https://offsets.imtheo.lol                  
/* -------------------------------------------------------------
/*  Dumped With     : RbxDumperV2                               
/*  Source code     : https://git.imtheo.lol/theo/RbxDumperV2   
/*  Roblox Version  : version-c5aecda2245e4fae
/*  Dumper Version  : 2.2.3
/*  Dumped At       : 17:54 09/09/2026 (GMT)
/*  Total Offsets   : 392
/* -------------------------------------------------------------
/*  Join the discord!                                           
/*  https://offsets.imtheo.lol/discord                          
/* =============================================================
*/



#include <cstdint>

#include <string>

namespace Offsets {

    inline std::string ClientVersion = "version-c5aecda2245e4fae";
    inline constexpr uintptr_t BaseAddress = 0x30;
    inline void update() {}




    namespace AirProperties {

         inline constexpr uintptr_t AirDensity = 0x18;

         inline constexpr uintptr_t GlobalWind = 0x3c;

    }



    namespace AnimationTrack {

         inline constexpr uintptr_t Animation = 0xb8;

         inline constexpr uintptr_t Animator = 0x110;

         inline constexpr uintptr_t IsPlaying = 0xa58;

         inline constexpr uintptr_t Looped = 0xe5;

         inline constexpr uintptr_t Speed = 0xd4;

         inline constexpr uintptr_t TimePosition = 0xd8;

    }



    namespace Animator {

         inline constexpr uintptr_t ActiveAnimations = 0xab0;

    }



    namespace Atmosphere {

         inline constexpr uintptr_t Color = 0xb8;

         inline constexpr uintptr_t Decay = 0xc4;

         inline constexpr uintptr_t Density = 0xd0;

         inline constexpr uintptr_t Glare = 0xd4;

         inline constexpr uintptr_t Haze = 0xd8;

         inline constexpr uintptr_t Offset = 0xdc;

    }



    namespace Attachment {

         inline constexpr uintptr_t Position = 0xc4;

    }



    namespace BasePart {

         inline constexpr uintptr_t CastShadow = 0x135;

         inline constexpr uintptr_t Color3 = 0x1a8;

         inline constexpr uintptr_t Locked = 0x136;

         inline constexpr uintptr_t Massless = 0x137;

         inline constexpr uintptr_t Primitive = 0x188;

         inline constexpr uintptr_t Reflectance = 0x10c;

         inline constexpr uintptr_t Shape = 0x1b8;

         inline constexpr uintptr_t Transparency = 0x130;

    }



    namespace Beam {

         inline constexpr uintptr_t Attachment0 = 0x160;

         inline constexpr uintptr_t Attachment1 = 0x170;

         inline constexpr uintptr_t Brightness = 0x180;

         inline constexpr uintptr_t CurveSize0 = 0x184;

         inline constexpr uintptr_t CurveSize1 = 0x188;

         inline constexpr uintptr_t LightEmission = 0x18c;

         inline constexpr uintptr_t LightInfluence = 0x190;

         inline constexpr uintptr_t Texture = 0x140;

         inline constexpr uintptr_t TextureLength = 0x19c;

         inline constexpr uintptr_t TextureSpeed = 0x1a4;

         inline constexpr uintptr_t Width0 = 0x1a8;

         inline constexpr uintptr_t Width1 = 0x1ac;

         inline constexpr uintptr_t ZOffset = 0x1b0;

    }



    namespace BloomEffect {

         inline constexpr uintptr_t Enabled = 0xb0;

         inline constexpr uintptr_t Intensity = 0xb8;

         inline constexpr uintptr_t Size = 0xbc;

         inline constexpr uintptr_t Threshold = 0xc0;

    }



    namespace BlurEffect {

         inline constexpr uintptr_t Enabled = 0xb0;

         inline constexpr uintptr_t Size = 0xb8;

    }



    namespace ByteCode {

         inline constexpr uintptr_t Pointer = 0x10;

         inline constexpr uintptr_t Size = 0x28;

    }



    namespace CachedItem {

         inline constexpr uintptr_t FileMeshData = 0x28;

    }



    namespace FileMeshData {

         inline constexpr uintptr_t AABBMax = 0x18c;

         inline constexpr uintptr_t AABBMin = 0x180;

         inline constexpr uintptr_t Faces = 0x30;

         inline constexpr uintptr_t FacesEnd = 0x38;

         inline constexpr uintptr_t Vertices = 0x0;

         inline constexpr uintptr_t VerticesEnd = 0x8;

    }



    namespace LRUHolder {

         inline constexpr uintptr_t MemEnforcedLRUCache = 0x20;

    }



    namespace LRUNode {

         inline constexpr uintptr_t AssetID = 0x10;

         inline constexpr uintptr_t CachedItem = 0x38;

         inline constexpr uintptr_t Next = 0x0;

    }



    namespace MemEnforcedLRUCache {

         inline constexpr uintptr_t Head = 0x8;

    }





    namespace Camera {

         inline constexpr uintptr_t CameraSubject = 0xc8;

         inline constexpr uintptr_t CameraType = 0x138;

         inline constexpr uintptr_t FieldOfView = 0x140;

         inline constexpr uintptr_t ImagePlaneDepth = 0x2d4;

         inline constexpr uintptr_t Position = 0xfc;

         inline constexpr uintptr_t Rotation = 0xd8;

         inline constexpr uintptr_t Viewport = 0x28c;

         inline constexpr uintptr_t ViewportSize = 0x2cc;

    }



    namespace CharacterMesh {

         inline constexpr uintptr_t BaseTextureId = 0xc8;

         inline constexpr uintptr_t BodyPart = 0x148;

         inline constexpr uintptr_t MeshId = 0xf8;

         inline constexpr uintptr_t OverlayTextureId = 0x128;

    }



    namespace ClickDetector {

         inline constexpr uintptr_t MaxActivationDistance = 0xe8;

         inline constexpr uintptr_t MouseIcon = 0xc8;

    }



    namespace Clothing {

         inline constexpr uintptr_t Color3 = 0x120;

         inline constexpr uintptr_t Template = 0x100;

    }



    namespace ColorCorrectionEffect {

         inline constexpr uintptr_t Brightness = 0xc4;

         inline constexpr uintptr_t Contrast = 0xc8;

         inline constexpr uintptr_t Enabled = 0xb0;

         inline constexpr uintptr_t TintColor = 0xb8;

    }



    namespace ColorGradingEffect {

         inline constexpr uintptr_t Enabled = 0xb0;

         inline constexpr uintptr_t TonemapperPreset = 0xb8;

    }



    namespace DataModel {

         inline constexpr uintptr_t CreatorId = 0x180;

         inline constexpr uintptr_t GameId = 0x188;

         inline constexpr uintptr_t GameLoaded = 0x5d8;

         inline constexpr uintptr_t JobId = 0x118;

         inline constexpr uintptr_t PlaceId = 0x190;

         inline constexpr uintptr_t PlaceVersion = 0x1ac;

         inline constexpr uintptr_t PrimitiveCount = 0x420;

         inline constexpr uintptr_t ScriptContext = 0x440;

         inline constexpr uintptr_t ServerIP = 0x5c0;

         inline constexpr uintptr_t ToRenderView1 = 0x1c8;

         inline constexpr uintptr_t ToRenderView2 = 0x8;

         inline constexpr uintptr_t ToRenderView3 = 0x28;

         inline constexpr uintptr_t Workspace = 0x158;

    }



    namespace DepthOfFieldEffect {

         inline constexpr uintptr_t Enabled = 0xb0;

         inline constexpr uintptr_t FarIntensity = 0xb8;

         inline constexpr uintptr_t FocusDistance = 0xbc;

         inline constexpr uintptr_t InFocusRadius = 0xc0;

         inline constexpr uintptr_t NearIntensity = 0xc4;

    }



    namespace DragDetector {

         inline constexpr uintptr_t ActivatedCursorIcon = 0x1c0;

         inline constexpr uintptr_t CursorIcon = 0xc8;

         inline constexpr uintptr_t MaxActivationDistance = 0xe8;

         inline constexpr uintptr_t MaxDragAngle = 0x2a8;

         inline constexpr uintptr_t MaxDragTranslation = 0x26c;

         inline constexpr uintptr_t MaxForce = 0x2ac;

         inline constexpr uintptr_t MaxTorque = 0x2b0;

         inline constexpr uintptr_t MinDragAngle = 0x2b4;

         inline constexpr uintptr_t MinDragTranslation = 0x278;

         inline constexpr uintptr_t ReferenceInstance = 0x1f0;

         inline constexpr uintptr_t Responsiveness = 0x2c0;

    }



    namespace FakeDataModel {

         inline constexpr uintptr_t Pointer = 0x8dc2258;

         inline constexpr uintptr_t RealDataModel = 0x1f8;

    }



    namespace GuiBase2D {

         inline constexpr uintptr_t AbsolutePosition = 0x10c;

         inline constexpr uintptr_t AbsoluteRotation = 0xe8;

         inline constexpr uintptr_t AbsoluteSize = 0x0;

    }



    namespace GuiObject {

         inline constexpr uintptr_t BackgroundColor3 = 0x540;

         inline constexpr uintptr_t BackgroundTransparency = 0x54c;

         inline constexpr uintptr_t BorderColor3 = 0x54c;

         inline constexpr uintptr_t Image = 0x9a0;

         inline constexpr uintptr_t LayoutOrder = 0x57c;

         inline constexpr uintptr_t Position = 0x510;

         inline constexpr uintptr_t RichText = 0xb98;

         inline constexpr uintptr_t Rotation = 0xe8;

         inline constexpr uintptr_t ScreenGui_Enabled = 0x4c4;

         inline constexpr uintptr_t Size = 0x530;

         inline constexpr uintptr_t Text = 0xe00;

         inline constexpr uintptr_t TextColor3 = 0xeb0;

         inline constexpr uintptr_t Visible = 0x5ad;

         inline constexpr uintptr_t ZIndex = 0x5a4;

    }



    namespace Humanoid {

         inline constexpr uintptr_t AutoJumpEnabled = 0x1d4;

         inline constexpr uintptr_t AutoRotate = 0x1d5;

         inline constexpr uintptr_t AutomaticScalingEnabled = 0x1d6;

         inline constexpr uintptr_t BreakJointsOnDeath = 0x1d7;

         inline constexpr uintptr_t CameraOffset = 0x128;

         inline constexpr uintptr_t DisplayDistanceType = 0x180;

         inline constexpr uintptr_t DisplayName = 0xb8;

         inline constexpr uintptr_t EvaluateStateMachine = 0x1d8;

         inline constexpr uintptr_t FloorMaterial = 0x184;

         inline constexpr uintptr_t Health = 0x190;

         inline constexpr uintptr_t HealthDisplayDistance = 0x188;

         inline constexpr uintptr_t HealthDisplayType = 0x18c;

         inline constexpr uintptr_t HipHeight = 0x194;

         inline constexpr uintptr_t HumanoidRootPart = 0x470;

         inline constexpr uintptr_t HumanoidState = 0x8b8;

         inline constexpr uintptr_t HumanoidStateID = 0x20;

         inline constexpr uintptr_t IsWalking = 0x95f;

         inline constexpr uintptr_t Jump = 0x1da;

         inline constexpr uintptr_t JumpHeight = 0x1a0;

         inline constexpr uintptr_t JumpPower = 0x1a4;

         inline constexpr uintptr_t MaxHealth = 0x1a8;

         inline constexpr uintptr_t MaxSlopeAngle = 0x1ac;

         inline constexpr uintptr_t MoveDirection = 0x140;

         inline constexpr uintptr_t MoveToPart = 0x118;

         inline constexpr uintptr_t MoveToPoint = 0x164;

         inline constexpr uintptr_t NameDisplayDistance = 0x1b0;

         inline constexpr uintptr_t NameOcclusion = 0x1b4;

         inline constexpr uintptr_t PlatformStand = 0x1dc;

         inline constexpr uintptr_t PlatformStatePointer = 0x0;

         inline constexpr uintptr_t RequiresNeck = 0x1dd;

         inline constexpr uintptr_t RigType = 0x1c0;

         inline constexpr uintptr_t SeatPart = 0x108;

         inline constexpr uintptr_t Sit = 0x1dd;

         inline constexpr uintptr_t TargetPoint = 0x14c;

         inline constexpr uintptr_t UseJumpPower = 0x1e0;

         inline constexpr uintptr_t WalkTimer = 0x400;

         inline constexpr uintptr_t Walkspeed = 0x1d0;
         inline constexpr uintptr_t WalkSpeed = 0x1d0;
         inline constexpr uintptr_t WalkSpeedCheck = 0x3b4;

         inline constexpr uintptr_t WalkspeedCheck = 0x3b4;

    }



    namespace Instance {
         inline constexpr uintptr_t Creator_create = 0x0;
         inline constexpr uintptr_t Creator_isCreatable = 0x10;
         inline constexpr uintptr_t FromExisting = 0x417c6b0;
         inline constexpr uintptr_t New = 0x417b8d0;
         inline constexpr uintptr_t SetParent = 0x7b4840;
         inline constexpr uintptr_t ClassByName = 0x46f6bfe;


         inline constexpr uintptr_t ChildrenEnd = 0x8;

         inline constexpr uintptr_t ChildrenStart = 0x78;

         inline constexpr uintptr_t ClassBase = 0x1b0;

         inline constexpr uintptr_t ClassDescriptor = 0x18;

         inline constexpr uintptr_t ClassName = 0x8;

         inline constexpr uintptr_t Name = 0x8;

         inline constexpr uintptr_t NameContainer = 0x70;

         inline constexpr uintptr_t Parent = 0x68;

         inline constexpr uintptr_t This = 0x8;

    }



    namespace Lighting {

         inline constexpr uintptr_t Ambient = 0xd0;

         inline constexpr uintptr_t Brightness = 0x118;

         inline constexpr uintptr_t ClockTime = 0xc8;

         inline constexpr uintptr_t ColorShift_Bottom = 0xe8;

         inline constexpr uintptr_t ColorShift_Top = 0xdc;

         inline constexpr uintptr_t EnvironmentDiffuseScale = 0x11c;

         inline constexpr uintptr_t EnvironmentSpecularScale = 0x120;

         inline constexpr uintptr_t ExposureCompensation = 0x124;

         inline constexpr uintptr_t FogColor = 0xf4;

         inline constexpr uintptr_t FogEnd = 0x12c;

         inline constexpr uintptr_t FogStart = 0x130;

         inline constexpr uintptr_t GeographicLatitude = 0x134;

         inline constexpr uintptr_t GlobalShadows = 0x144;

         inline constexpr uintptr_t GradientBottom = 0x190;

         inline constexpr uintptr_t GradientTop = 0x150;

         inline constexpr uintptr_t LightColor = 0x15c;

         inline constexpr uintptr_t LightDirection = 0x168;

         inline constexpr uintptr_t MoonPosition = 0x184;

         inline constexpr uintptr_t OutdoorAmbient = 0x100;

         inline constexpr uintptr_t Sky = 0x1c8;

         inline constexpr uintptr_t Source = 0x174;

         inline constexpr uintptr_t SunPosition = 0x178;

    }



    namespace LocalScript {

         inline constexpr uintptr_t ByteCode = 0x190;

         inline constexpr uintptr_t GUID = 0xd0;

         inline constexpr uintptr_t Hash = 0x1a0;

    }



    namespace MaterialColors {

         inline constexpr uintptr_t Asphalt = 0x30;

         inline constexpr uintptr_t Basalt = 0x27;

         inline constexpr uintptr_t Brick = 0xf;

         inline constexpr uintptr_t Cobblestone = 0x33;

         inline constexpr uintptr_t Concrete = 0xc;

         inline constexpr uintptr_t CrackedLava = 0x2d;

         inline constexpr uintptr_t Glacier = 0x1b;

         inline constexpr uintptr_t Grass = 0x6;

         inline constexpr uintptr_t Ground = 0x2a;

         inline constexpr uintptr_t Ice = 0x36;

         inline constexpr uintptr_t LeafyGrass = 0x39;

         inline constexpr uintptr_t Limestone = 0x3f;

         inline constexpr uintptr_t Mud = 0x24;

         inline constexpr uintptr_t Pavement = 0x42;

         inline constexpr uintptr_t Rock = 0x18;

         inline constexpr uintptr_t Salt = 0x3c;

         inline constexpr uintptr_t Sand = 0x12;

         inline constexpr uintptr_t Sandstone = 0x21;

         inline constexpr uintptr_t Slate = 0x9;

         inline constexpr uintptr_t Snow = 0x1e;

         inline constexpr uintptr_t WoodPlanks = 0x15;

    }



    namespace MeshContentProvider {

         inline constexpr uintptr_t LRUHolder = 0xd8;

    }



    namespace MeshData {

         inline constexpr uintptr_t FaceEnd = 0x38;

         inline constexpr uintptr_t FaceStart = 0x30;

         inline constexpr uintptr_t VertexEnd = 0x8;

         inline constexpr uintptr_t VertexStart = 0x0;

    }



    namespace MeshPart {

         inline constexpr uintptr_t MeshId = 0x310;

         inline constexpr uintptr_t Texture = 0x340;

    }



    namespace Misc {

         inline constexpr uintptr_t Adornee = 0xf0;

         inline constexpr uintptr_t AnimationId = 0xc0;

         inline constexpr uintptr_t StringLength = 0x10;

         inline constexpr uintptr_t Value = 0xb8;

    }



    namespace Model {

         inline constexpr uintptr_t PrimaryPart = 0x258;

         inline constexpr uintptr_t Scale = 0x144;

    }



    namespace ModuleScript {

         inline constexpr uintptr_t ByteCode = 0x138;

         inline constexpr uintptr_t GUID = 0xd0;

         inline constexpr uintptr_t Hash = 0x148;

         inline constexpr uintptr_t IsCoreScript = 0x0;

    }



    namespace MouseService {

         inline constexpr uintptr_t InputObject = 0xf0;

         inline constexpr uintptr_t InputObject2 = 0x100;

         inline constexpr uintptr_t MousePosition = 0xd4;

         inline constexpr uintptr_t SensitivityPointer = 0x0;

    }



    namespace ParticleEmitter {

         inline constexpr uintptr_t Acceleration = 0x1e0;

         inline constexpr uintptr_t Brightness = 0x21c;

         inline constexpr uintptr_t Drag = 0x220;

         inline constexpr uintptr_t Lifetime = 0x1f4;

         inline constexpr uintptr_t LightEmission = 0x238;

         inline constexpr uintptr_t LightInfluence = 0x23c;

         inline constexpr uintptr_t Rate = 0x248;

         inline constexpr uintptr_t RotSpeed = 0x1fc;

         inline constexpr uintptr_t Rotation = 0x204;

         inline constexpr uintptr_t Speed = 0x20c;

         inline constexpr uintptr_t SpreadAngle = 0x214;

         inline constexpr uintptr_t Texture = 0x1c0;

         inline constexpr uintptr_t TimeScale = 0x25c;

         inline constexpr uintptr_t VelocityInheritance = 0x260;

         inline constexpr uintptr_t ZOffset = 0x264;

    }



    namespace Player {

         inline constexpr uintptr_t AccountAge = 0x35c;

         inline constexpr uintptr_t CameraMode = 0x370;

         inline constexpr uintptr_t Character = 0x298;

         inline constexpr uintptr_t DisplayName = 0x138;

         inline constexpr uintptr_t HealthDisplayDistance = 0x394;

         inline constexpr uintptr_t LocalPlayer = 0x130;

         inline constexpr uintptr_t LocaleId = 0x118;

         inline constexpr uintptr_t MaxZoomDistance = 0x368;

         inline constexpr uintptr_t MinZoomDistance = 0x36c;

         inline constexpr uintptr_t ModelInstance = 0x298;

         inline constexpr uintptr_t Mouse = 0x1208;

         inline constexpr uintptr_t NameDisplayDistance = 0x3a4;

         inline constexpr uintptr_t Team = 0x2d8;

         inline constexpr uintptr_t TeamColor = 0x3b0;

         inline constexpr uintptr_t UserId = 0xd0;

    }



    namespace PlayerConfigurer {

         inline constexpr uintptr_t Pointer = 0x0;

    }



    namespace PlayerMouse {

         inline constexpr uintptr_t Icon = 0xc8;

         inline constexpr uintptr_t Workspace = 0x150;

    }



    namespace Primitive {

         inline constexpr uintptr_t AssemblyAngularVelocity = 0x104;

         inline constexpr uintptr_t AssemblyLinearVelocity = 0xf8;

         inline constexpr uintptr_t Flags = 0x1b6;

         inline constexpr uintptr_t Material = 0x0;

         inline constexpr uintptr_t Owner = 0x210;

         inline constexpr uintptr_t Position = 0xec;

         inline constexpr uintptr_t Rotation = 0xc8;

         inline constexpr uintptr_t Size = 0x1bc;

         inline constexpr uintptr_t Validate = 0x6;

    }



    namespace PrimitiveFlags {

         inline constexpr uintptr_t Anchored = 0x2;

         inline constexpr uintptr_t CanCollide = 0x8;

         inline constexpr uintptr_t CanQuery = 0x20;

         inline constexpr uintptr_t CanTouch = 0x10;

    }



    namespace ProximityPrompt {

         inline constexpr uintptr_t ActionText = 0xb0;

         inline constexpr uintptr_t Enabled = 0x136;

         inline constexpr uintptr_t GamepadKeyCode = 0x11c;

         inline constexpr uintptr_t HoldDuration = 0x120;

         inline constexpr uintptr_t KeyCode = 0x124;

         inline constexpr uintptr_t MaxActivationDistance = 0x128;

         inline constexpr uintptr_t ObjectText = 0xd0;

         inline constexpr uintptr_t RequiresLineOfSight = 0x137;

    }



    namespace RenderJob {

         inline constexpr uintptr_t FakeDataModel = 0x38;

         inline constexpr uintptr_t RealDataModel = 0x1f0;

         inline constexpr uintptr_t RenderView = 0x1d8;

    }



    namespace RenderView {

         inline constexpr uintptr_t DeviceD3D11 = 0x0;

         inline constexpr uintptr_t LightingValid = 0x0;

         inline constexpr uintptr_t SkyValid = 0x0;

         inline constexpr uintptr_t VisualEngine = 0x0;

    }



    namespace RunService {

         inline constexpr uintptr_t HeartbeatFPS = 0xc0;

         inline constexpr uintptr_t HeartbeatTask = 0xe0;

    }



    namespace Script {

         inline constexpr uintptr_t ByteCode = 0x190;

         inline constexpr uintptr_t GUID = 0xd0;

         inline constexpr uintptr_t Hash = 0x1a0;

    }



    namespace ScriptContext {

         inline constexpr uintptr_t RequireBypass = 0x0;

    }



    namespace Seat {

         inline constexpr uintptr_t Occupant = 0x218;

    }



    namespace Sky {

         inline constexpr uintptr_t MoonAngularSize = 0x244;

         inline constexpr uintptr_t MoonTextureId = 0xc8;

         inline constexpr uintptr_t SkyboxBk = 0xf8;

         inline constexpr uintptr_t SkyboxDn = 0x128;

         inline constexpr uintptr_t SkyboxFt = 0x158;

         inline constexpr uintptr_t SkyboxLf = 0x188;

         inline constexpr uintptr_t SkyboxOrientation = 0x238;

         inline constexpr uintptr_t SkyboxRt = 0x1b8;

         inline constexpr uintptr_t SkyboxUp = 0x1e8;

         inline constexpr uintptr_t StarCount = 0x248;

         inline constexpr uintptr_t SunAngularSize = 0x23c;

         inline constexpr uintptr_t SunTextureId = 0x218;

    }



    namespace Sound {

         inline constexpr uintptr_t IsPlaying = 0x140;

         inline constexpr uintptr_t Looped = 0x13d;

         inline constexpr uintptr_t PlaybackSpeed = 0x11c;

         inline constexpr uintptr_t RollOffMaxDistance = 0x120;

         inline constexpr uintptr_t RollOffMinDistance = 0x124;

         inline constexpr uintptr_t SoundGroup = 0xe8;

         inline constexpr uintptr_t SoundId = 0xc8;

         inline constexpr uintptr_t Volume = 0x130;

    }



    namespace SpawnLocation {

         inline constexpr uintptr_t AllowTeamChangeOnTouch = 0x1f0;

         inline constexpr uintptr_t Enabled = 0x1f1;

         inline constexpr uintptr_t ForcefieldDuration = 0x1e8;

         inline constexpr uintptr_t Neutral = 0x1f2;

         inline constexpr uintptr_t TeamColor = 0x1ec;

    }



    namespace SpecialMesh {

         inline constexpr uintptr_t MeshId = 0xf8;

         inline constexpr uintptr_t Offset = 0xb8;

         inline constexpr uintptr_t Scale = 0xc4;

         inline constexpr uintptr_t TextureId = 0x128;

    }



    namespace StatsItem {

         inline constexpr uintptr_t Value = 0x1091;

    }



    namespace SunRaysEffect {

         inline constexpr uintptr_t Enabled = 0xb0;

         inline constexpr uintptr_t Intensity = 0xb8;

         inline constexpr uintptr_t Spread = 0xbc;

    }



    namespace SurfaceAppearance {

         inline constexpr uintptr_t AlphaMode = 0x1f0;

         inline constexpr uintptr_t Color = 0x1d8;

         inline constexpr uintptr_t ColorMap = 0xc8;

         inline constexpr uintptr_t EmissiveMaskContent = 0xf8;

         inline constexpr uintptr_t EmissiveStrength = 0x1f4;

         inline constexpr uintptr_t EmissiveTint = 0x1e4;

         inline constexpr uintptr_t MetalnessMap = 0x128;

         inline constexpr uintptr_t NormalMap = 0x158;

         inline constexpr uintptr_t RoughnessMap = 0x188;

    }



    namespace TaskScheduler {

         inline constexpr uintptr_t JobEnd = 0xd0;

         inline constexpr uintptr_t JobName = 0x18;

         inline constexpr uintptr_t JobStart = 0xc8;

         inline constexpr uintptr_t MaxFPS = 0xb0;

         inline constexpr uintptr_t Pointer = 0x8b5cee8;

    }



    namespace Team {

         inline constexpr uintptr_t BrickColor = 0xb8;

    }



    namespace Terrain {

         inline constexpr uintptr_t GrassLength = 0x1f0;

         inline constexpr uintptr_t MaterialColors = 0x4b8;

         inline constexpr uintptr_t WaterColor = 0x1e0;

         inline constexpr uintptr_t WaterReflectance = 0x1f8;

         inline constexpr uintptr_t WaterTransparency = 0x1fc;

         inline constexpr uintptr_t WaterWaveSize = 0x200;

         inline constexpr uintptr_t WaterWaveSpeed = 0x204;

    }



    namespace Textures {

         inline constexpr uintptr_t Decal_Texture = 0x1e0;

         inline constexpr uintptr_t Texture_Texture = 0x1e0;

    }



    namespace Tool {

         inline constexpr uintptr_t CanBeDropped = 0x4b8;

         inline constexpr uintptr_t Enabled = 0x4b9;

         inline constexpr uintptr_t Grip = 0x4ac;

         inline constexpr uintptr_t ManualActivationOnly = 0x4ba;

         inline constexpr uintptr_t RequiresHandle = 0x4bb;

         inline constexpr uintptr_t TextureId = 0x360;

         inline constexpr uintptr_t Tooltip = 0x468;

    }



    namespace UnionOperation {

         inline constexpr uintptr_t AssetId = 0x310;

    }



    namespace UserInputService {

         inline constexpr uintptr_t WindowInputState = 0x2c0;

    }



    namespace VehicleSeat {

         inline constexpr uintptr_t MaxSpeed = 0x228;

         inline constexpr uintptr_t SteerFloat = 0x22c;

         inline constexpr uintptr_t ThrottleFloat = 0x230;

         inline constexpr uintptr_t Torque = 0x234;

         inline constexpr uintptr_t TurnSpeed = 0x238;

    }



    namespace VisualEngine {

         inline constexpr uintptr_t Dimensions = 0xb10;

         inline constexpr uintptr_t FakeDataModel = 0xaf0;

         inline constexpr uintptr_t Pointer = 0x83f0a08;

         inline constexpr uintptr_t RenderView = 0xc30;

         inline constexpr uintptr_t ViewMatrix = 0x1b0;

    }



    namespace Weld {

         inline constexpr uintptr_t Part0 = 0x118;

         inline constexpr uintptr_t Part1 = 0x128;

    }



    namespace WeldConstraint {

         inline constexpr uintptr_t Part0 = 0xb8;

         inline constexpr uintptr_t Part1 = 0xc8;

    }



    namespace WindowInputState {

         inline constexpr uintptr_t CapsLock = 0x40;

         inline constexpr uintptr_t CurrentTextBox = 0x48;

    }



    namespace Workspace {

         inline constexpr uintptr_t CurrentCamera = 0x4b8;

         inline constexpr uintptr_t DistributedGameTime = 0x4d8;

         inline constexpr uintptr_t ReadOnlyGravity = 0xa00;

         inline constexpr uintptr_t World = 0x410;

    }



    namespace World {

         inline constexpr uintptr_t AirProperties = 0x240;

         inline constexpr uintptr_t FallenPartsDestroyHeight = 0x220;

         inline constexpr uintptr_t Gravity = 0x22c;

         inline constexpr uintptr_t Primitives = 0x2b0;

         inline constexpr uintptr_t worldStepsPerSec = 0x728;

    }





    namespace Alloc {
         inline constexpr uintptr_t Malloc = 0x7b41d0;
    }


    namespace Attribute {
         inline constexpr uintptr_t Key = 0x0;
         inline constexpr uintptr_t Size = 0x58;
         inline constexpr uintptr_t Type = 0x18;
         inline constexpr uintptr_t TypeIdRva = 0x8806404;
         inline constexpr uintptr_t TypeIdRvaNew = 0x88063b4;
         inline constexpr uintptr_t Value = 0x8;
    }


    namespace AttributesMap {
         inline constexpr uintptr_t Attributes = 0x10;
         inline constexpr uintptr_t Length = 0x0;
    }


    namespace Chat {
         inline constexpr uintptr_t IsFocused = 0x154;
    }


    namespace FastClusterEntity {
         inline constexpr uintptr_t AlphaByte = 0x14;
         inline constexpr uintptr_t BBoxMaxX = 0xa4;
         inline constexpr uintptr_t BBoxMaxY = 0xa8;
         inline constexpr uintptr_t BBoxMaxZ = 0xac;
         inline constexpr uintptr_t BBoxMinX = 0x98;
         inline constexpr uintptr_t BBoxMinY = 0x9c;
         inline constexpr uintptr_t BBoxMinZ = 0xa0;
         inline constexpr uintptr_t ContextPtr = 0x8;
         inline constexpr uintptr_t DecalMaterialPtr = 0x48;
         inline constexpr uintptr_t MaterialPtr = 0x20;
         inline constexpr uintptr_t PrimitiveIndexArrayPtr = 0x80;
         inline constexpr uintptr_t RenderQueueId = 0x10;
         inline constexpr uintptr_t TechniqueArrayPtr = 0x70;
         inline constexpr uintptr_t VTableRva = 0x6b9a8b8;

         namespace Context {
              inline constexpr uintptr_t PrimitivePoolPtr = 0x1a0;
         }

         namespace PrimitivePool {
              inline constexpr uintptr_t ArrayBase = 0x20;
         }

         namespace PrimitiveRecord {
              inline constexpr uintptr_t Stride = 0x30;
              inline constexpr uintptr_t Translation = 0x24;
         }
    }


    namespace Highlight {
         inline constexpr uintptr_t FillColor = 0xc8;
         inline constexpr uintptr_t FillTransparency = 0xe4;
         inline constexpr uintptr_t OutlineColor = 0xd4;
         inline constexpr uintptr_t OutlineTransparency = 0xec;
         inline constexpr uintptr_t DepthMode = 0xe0;
         inline constexpr uintptr_t Enabled = 0xf4;
         inline constexpr uintptr_t Adornee = 0xb8;
         inline constexpr uintptr_t AdorneeControl = 0xc0;
         inline constexpr uintptr_t AdorneeGetSet = 0xb0;
    }


    namespace InputObject {
         inline constexpr uintptr_t MousePosition = 0xd4;
    }


    namespace LightingParameters {
         inline constexpr uintptr_t GeographicLatitude = 0x134;
         inline constexpr uintptr_t LightColor = 0x15c;
         inline constexpr uintptr_t LightDirection = 0x168;
         inline constexpr uintptr_t SkyAmbient = 0x150;
         inline constexpr uintptr_t SkyAmbient2 = 0x138;
         inline constexpr uintptr_t Source = 0x174;
         inline constexpr uintptr_t TrueMoonPosition = 0x184;
         inline constexpr uintptr_t TrueSunPosition = 0x178;
    }


    namespace LuaState {
         inline constexpr uintptr_t Base = 0x28;
         inline constexpr uintptr_t Global = 0x20;
         inline constexpr uintptr_t Top = 0x8;
         inline constexpr uintptr_t TypeTag = 0x0;
    }


    namespace Luau {
         inline constexpr uintptr_t loadstring = 0x411b342;
         inline constexpr uintptr_t lua_getglobal = 0x0;
         inline constexpr uintptr_t require_impl = 0x0;
    }


    namespace LuauGlobal {
         inline constexpr uintptr_t currentwhite = 0x58;
         inline constexpr uintptr_t dummynode = 0x8806310;
         inline constexpr uintptr_t gcopages = 0x2f0;
         inline constexpr uintptr_t gcopages_end = 0x0;
         inline constexpr uintptr_t gcopages_large = 0x2f0;
         inline constexpr uintptr_t gcpause = 0x38;
         inline constexpr uintptr_t gcstate = 0x59;
         inline constexpr uintptr_t gcstepmul = 0x3c;
         inline constexpr uintptr_t gcstepsize = 0x40;
         inline constexpr uintptr_t GCthreshold = 0x48;
         inline constexpr uintptr_t gray = 0x10;
         inline constexpr uintptr_t grayagain = 0x18;
         inline constexpr uintptr_t page_next_all = 0x8;
         inline constexpr uintptr_t page_next_free = 0x18;
         inline constexpr uintptr_t strt_hash = 0x0;
         inline constexpr uintptr_t strt_size = 0xc;
         inline constexpr uintptr_t totalbytes = 0x50;
         inline constexpr uintptr_t weak = 0x20;
    }


    namespace LuauObject {
         inline constexpr uintptr_t marked = 0x2;
         inline constexpr uintptr_t page_block = 0x24;
         inline constexpr uintptr_t page_data = 0x40;
         inline constexpr uintptr_t page_next = 0x8;
         inline constexpr uintptr_t page_size = 0x20;
         inline constexpr uintptr_t table_array = 0x28;
         inline constexpr uintptr_t table_gclist = 0x20;
         inline constexpr uintptr_t table_lsz = 0x7;
         inline constexpr uintptr_t table_node = 0x18;
         inline constexpr uintptr_t table_sizearray = 0x8;
         inline constexpr uintptr_t tt = 0x0;
    }


    namespace MaterialLayer {
         inline constexpr uintptr_t ColorData = 0x24;
         inline constexpr uintptr_t FillModeByte = 0x11;
         inline constexpr uintptr_t Flags2 = 0x20;
         inline constexpr uintptr_t MatFlags = 0x18;
         inline constexpr uintptr_t Param = 0x1c;
         inline constexpr uintptr_t Stride = 0x88;
    }


    namespace Players {
         inline constexpr uintptr_t LocalPlayer = 0x130;
    }


    namespace Reflection {
         inline constexpr uintptr_t ClassDescCreatable = 0x10;
         inline constexpr uintptr_t ClassDescFlags = 0x1bc;
         inline constexpr uintptr_t CreatorTable = 0x88a8378;
         inline constexpr uintptr_t EntryValue = 0x8;
         inline constexpr uintptr_t NameRegistry = 0x87a13c8;
         inline constexpr uintptr_t NameTable = 0x50;
         inline constexpr uintptr_t TableEmpty = 0x20;
         inline constexpr uintptr_t TableEnd = 0x8;
         inline constexpr uintptr_t TableStart = 0x0;
         inline constexpr uintptr_t TableStride = 0x10;
    }


    namespace RenderQueue {
         inline constexpr uintptr_t AlwaysOnTop = 0xd;
         inline constexpr uintptr_t AlwaysOnTopAdorns = 0xe;
         inline constexpr uintptr_t Decals = 0x2;
         inline constexpr uintptr_t Glass = 0x8;
         inline constexpr uintptr_t GlassTint = 0x7;
         inline constexpr uintptr_t OnTopReadOnlyDepth = 0xc;
         inline constexpr uintptr_t OnTopWithDepth = 0xb;
         inline constexpr uintptr_t Opaque = 0x0;
         inline constexpr uintptr_t OpaqueAdorns = 0x4;
         inline constexpr uintptr_t OpaqueCasters = 0x3;
         inline constexpr uintptr_t OpaqueWithAlpha = 0x5;
         inline constexpr uintptr_t Screen = 0xf;
         inline constexpr uintptr_t ScreenOnTopOfBlur = 0x10;
         inline constexpr uintptr_t Terrain = 0x1;
         inline constexpr uintptr_t Transparent = 0x9;
         inline constexpr uintptr_t TransparentCasters = 0xa;
         inline constexpr uintptr_t Water = 0x6;
    }


    namespace RobloxString {
         inline constexpr uintptr_t Size = 0x10;
         inline constexpr uintptr_t SsoCapacity = 0xf;
    }


    namespace TechniqueArray {
         inline constexpr uintptr_t BeginOffset = 0x0;
         inline constexpr uintptr_t EndOffset = 0x8;
    }


    namespace TextButton {
         inline constexpr uintptr_t AutoButtonColor = 0x9c4;
         inline constexpr uintptr_t ContentText = 0xe08;
         inline constexpr uintptr_t Font = 0x6;
         inline constexpr uintptr_t LineHeight = 0xf0;
         inline constexpr uintptr_t LocalizedText = 0xe08;
         inline constexpr uintptr_t MaxVisibleGraphemes = 0xb8;
         inline constexpr uintptr_t Modal = 0x9c5;
         inline constexpr uintptr_t RichText = 0x6;
         inline constexpr uintptr_t Selected = 0x9c6;
         inline constexpr uintptr_t Text = 0xe08;
         inline constexpr uintptr_t TextColor3 = 0x1120;
         inline constexpr uintptr_t TextDirection = 0x6;
         inline constexpr uintptr_t TextScaled = 0x6;
         inline constexpr uintptr_t TextSize = 0x1144;
         inline constexpr uintptr_t TextStrokeColor3 = 0x14;
         inline constexpr uintptr_t TextStrokeTransparency = 0xf0;
         inline constexpr uintptr_t TextTransparency = 0x20;
         inline constexpr uintptr_t TextTruncate = 0x6;
         inline constexpr uintptr_t TextWrapped = 0x6;
         inline constexpr uintptr_t TextXAlignment = 0x669;
         inline constexpr uintptr_t TextYAlignment = 0xd;
    }


    namespace TextLabel {
         inline constexpr uintptr_t ContentText = 0xb88;
         inline constexpr uintptr_t Font = 0x6;
         inline constexpr uintptr_t LineHeight = 0xca0;
         inline constexpr uintptr_t LocalizedText = 0xb88;
         inline constexpr uintptr_t MaxVisibleGraphemes = 0xebc;
         inline constexpr uintptr_t RichText = 0xd9e;
         inline constexpr uintptr_t Text = 0xb88;
         inline constexpr uintptr_t TextColor3 = 0xea0;
         inline constexpr uintptr_t TextDirection = 0x6;
         inline constexpr uintptr_t TextScaled = 0xd96;
         inline constexpr uintptr_t TextSize = 0xec4;
         inline constexpr uintptr_t TextStrokeColor3 = 0xeac;
         inline constexpr uintptr_t TextStrokeTransparency = 0xec8;
         inline constexpr uintptr_t TextTransparency = 0xecc;
         inline constexpr uintptr_t TextTruncate = 0x6;
         inline constexpr uintptr_t TextWrapped = 0xd98;
         inline constexpr uintptr_t TextXAlignment = 0x669;
         inline constexpr uintptr_t TextYAlignment = 0xd;
    }


    namespace Value {
         inline constexpr uintptr_t Value = 0xb8;
    }


    namespace WorldRoot {
         inline constexpr uintptr_t RaycastBoundDesc = 0x8089f20;
         inline constexpr uintptr_t RaycastBoundFn = 0x80;
    }

}