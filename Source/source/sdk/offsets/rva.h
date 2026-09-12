#pragma once
#include <cstddef>
#include <cstdint>

namespace RVA {
    namespace Alloc {
        inline constexpr std::uintptr_t Free = 0x1790790;
        inline constexpr std::uintptr_t Malloc = 0x799600;
    }

    namespace Attribute {
        inline constexpr std::uintptr_t TypeIdRva = 0x867113c;
    }

    namespace Chams {
        inline constexpr bool Resolved = true;

        namespace RttiTypeDescriptors {
            inline constexpr std::uintptr_t FastClusterEntity = 0x7bb1438;
            inline constexpr std::uintptr_t RenderEntity = 0x7bace20;
        }

        namespace Confirmed {
            inline constexpr std::uintptr_t FastClusterEntityCOL = 0x6f99c78;
            inline constexpr std::uintptr_t FastClusterEntityVTable = 0x68cda58;
            inline constexpr std::uintptr_t RenderEntityCOL = 0x6f97a60;
            inline constexpr std::uintptr_t RenderEntityVTable = 0x68ccc50;
            inline constexpr std::uintptr_t FastClusterEntityObjectSize = 0x130;
            inline constexpr std::uintptr_t RenderEntityObjectSize = 0x80;
            inline constexpr std::uintptr_t RenderQueueNameTable = 0x5efbad0;
            inline constexpr std::uintptr_t OpenGLCullModeTable = 0x652d3a0;
            inline constexpr std::uintptr_t OpenGLFillModeTable = 0x652d3b0;
            inline constexpr std::uintptr_t OpenGLDepthFunctionTable = 0x652d3d8;
        }

        namespace Functions {
            inline constexpr std::uintptr_t TechniqueMoveRange = 0xf81070;
            inline constexpr std::uintptr_t TechniqueConstructor = 0xf814e0;
            inline constexpr std::uintptr_t TechniqueFind = 0xf816d0;
            inline constexpr std::uintptr_t TechniqueSetBlendState = 0xf81a10;
            inline constexpr std::uintptr_t TechniqueSetDepthState = 0xf81b30;
            inline constexpr std::uintptr_t TechniqueSetRasterizerState = 0xf81b40;
            inline constexpr std::uintptr_t RenderEntityConstructor = 0xf8fc80;
            inline constexpr std::uintptr_t RenderEntityDestructor = 0xf90060;
            inline constexpr std::uintptr_t RenderEntitySubmit = 0xf905b0;
            inline constexpr std::uintptr_t FastClusterBindingConstructor = 0x10d85e0;
            inline constexpr std::uintptr_t FastClusterEntityConstructor = 0x10d9020;
            inline constexpr std::uintptr_t FastClusterEntityDestructor = 0x10d9430;
            inline constexpr std::uintptr_t OpenGLApplyRenderStates = 0x36a2960;
        }

        namespace Fields {
            inline constexpr std::uintptr_t FastClusterEntityVTable = Confirmed::FastClusterEntityVTable;
            inline constexpr std::uintptr_t BasePartClusterSubBackref = 0x130;
            inline constexpr std::uintptr_t RenderEntityTechniqueArrayPtr = 0x70;
            inline constexpr std::uintptr_t RenderEntityRenderQueueId = 0x10;
            inline constexpr std::uint32_t RenderEntityQueueIdOpaque = 0;
            inline constexpr std::uint32_t RenderEntityQueueIdTerrain = 1;
            inline constexpr std::uint32_t RenderEntityQueueIdDecals = 2;
            inline constexpr std::uint32_t RenderEntityQueueIdOpaqueCasters = 3;
            inline constexpr std::uint32_t RenderEntityQueueIdOpaqueAdorns = 4;
            inline constexpr std::uint32_t RenderEntityQueueIdOpaqueWithAlpha = 5;
            inline constexpr std::uint32_t RenderEntityQueueIdWater = 6;
            inline constexpr std::uint32_t RenderEntityQueueIdGlassTint = 7;
            inline constexpr std::uint32_t RenderEntityQueueIdGlass = 8;
            inline constexpr std::uint32_t RenderEntityQueueIdTransparent = 9;
            inline constexpr std::uint32_t RenderEntityQueueIdTransparentCasters = 10;
            inline constexpr std::uint32_t RenderEntityQueueIdOnTopWithDepth = 11;
            inline constexpr std::uint32_t RenderEntityQueueIdOnTopReadOnlyDepth = 12;
            inline constexpr std::uint32_t RenderEntityQueueIdAlwaysOnTop = 13;
            inline constexpr std::uint32_t RenderEntityQueueIdAlwaysOnTopAdorns = 14;
            inline constexpr std::uint32_t RenderEntityQueueIdScreen = 15;
            inline constexpr std::uint32_t RenderEntityQueueIdScreenOnTopOfScene = 16;
            inline constexpr std::uintptr_t TechniqueArrayBegin = 0x00;
            inline constexpr std::uintptr_t TechniqueArrayEnd = 0x08;
            inline constexpr std::uintptr_t TechniqueStride = 0x88;

            inline constexpr std::uintptr_t TechniqueRasterizerState = 0x10;
            inline constexpr std::uintptr_t TechniqueCullMode = 0x10;
            inline constexpr std::uintptr_t TechniqueFillMode = 0x11;
            inline constexpr std::uintptr_t TechniqueDepthBias = 0x14;

            inline constexpr std::uintptr_t TechniqueDepthState = 0x18;
            inline constexpr std::uintptr_t TechniqueDepthFunction = 0x18;
            inline constexpr std::uintptr_t TechniqueDepthWrite = 0x19;
            inline constexpr std::uintptr_t TechniqueStencilMode = 0x1a;

            inline constexpr std::uintptr_t TechniqueBlendState = 0x1c;
            inline constexpr std::uintptr_t TechniqueBlendColorMask = 0x1c;
            inline constexpr std::uintptr_t TechniqueBlendColorSource = 0x20;
            inline constexpr std::uintptr_t TechniqueBlendColorDestination = 0x21;
            inline constexpr std::uintptr_t TechniqueBlendAlphaSource = 0x22;
            inline constexpr std::uintptr_t TechniqueBlendAlphaDestination = 0x23;
            inline constexpr std::uintptr_t TechniqueBlendAlphaToCoverage = 0x24;

            inline constexpr std::uint8_t TechniqueCullModeNone = 0;
            inline constexpr std::uint8_t TechniqueCullModeBack = 1;
            inline constexpr std::uint8_t TechniqueStageCullModeFront = 2;
            inline constexpr std::uint8_t TechniqueFillModeSolid = 0;
            inline constexpr std::uint8_t TechniqueFillModeWireframe = 1;
            inline constexpr std::uint8_t TechniqueDepthFunctionAlways = 0;
            inline constexpr std::uint8_t TechniqueDepthFunctionLess = 1;
            inline constexpr std::uint8_t TechniqueDepthFunctionLessEqual = 2;
            inline constexpr std::uint8_t TechniqueDepthFunctionGreater = 3;
            inline constexpr std::uint8_t TechniqueDepthFunctionGreaterEqual = 4;
            inline constexpr std::uint8_t TechniqueDepthFunctionEqual = 5;
            inline constexpr std::uint8_t TechniqueDepthFunctionNotEqual = 6;
            inline constexpr std::uint32_t TechniqueBlendColorMaskAll = 0x0f;

            inline constexpr std::uintptr_t MaterialLayerStride = TechniqueStride;
            inline constexpr std::uintptr_t MaterialLayerCullMode = TechniqueCullMode;
            inline constexpr std::uintptr_t MaterialLayerFillMode = TechniqueFillMode;
            inline constexpr std::uintptr_t MaterialLayerMatFlags = TechniqueDepthState;
            inline constexpr std::uintptr_t MaterialLayerParam = TechniqueBlendColorMask;
            inline constexpr std::uintptr_t MaterialLayerFlags2 = TechniqueBlendColorSource;
            inline constexpr std::uintptr_t MaterialLayerColorData = TechniqueBlendAlphaToCoverage;
            inline constexpr std::uint8_t MaterialLayerFillModeSolid = TechniqueFillModeSolid;
            inline constexpr std::uint8_t MaterialLayerFillModeWireframe = TechniqueFillModeWireframe;

            inline constexpr std::uintptr_t FceContextPtr = 0x08;
            inline constexpr std::uintptr_t FcePrimitiveIndexArrayPtr = 0x80;
            inline constexpr std::uintptr_t FceBBoxMin = 0x98;
            inline constexpr std::uintptr_t FceBBoxMax = 0xA4;
            inline constexpr std::uintptr_t FceContextPrimitivePoolPtr = 0x1A0;
            inline constexpr std::uintptr_t FcePrimitivePoolArrayBase = 0x20;
            inline constexpr std::uintptr_t FcePrimitiveRecordStride = 48;
            inline constexpr std::uintptr_t FcePrimitiveRecordTranslation = 36;
        }

        namespace Verified = Fields;
        namespace Pending = Fields;
    }

    namespace Creator {
        inline constexpr bool Resolved = true;

        namespace RttiTypeDescriptors {
            inline constexpr std::uintptr_t TaskSchedulerJob = 0x7b69f78;
        }

        namespace Confirmed {
            inline constexpr std::uintptr_t TaskSchedulerJobCOL = 0x6f5ff60;
            inline constexpr std::uintptr_t TaskSchedulerJobVTable = 0x68a9c80;
            inline constexpr std::size_t JobStepVtableIndex = 1;
            inline constexpr std::size_t HeartbeatVtableSlots = 64;
            inline constexpr std::uintptr_t CanonicalNameLookup = 0xd65fc0;
            inline constexpr std::uintptr_t ClassDescLookup = 0x8ec9b0;
            inline constexpr std::uintptr_t SetCreator = 0x8c6240;
            inline constexpr std::uintptr_t InstanceSetParent = 0x8b6d70;
        }

        namespace Pending {
            inline constexpr std::size_t JobStepVtableIndex = Confirmed::JobStepVtableIndex;
            inline constexpr std::size_t HeartbeatVtableSlots = Confirmed::HeartbeatVtableSlots;
            inline constexpr std::uintptr_t CanonicalNameLookup = Confirmed::CanonicalNameLookup;
            inline constexpr std::uintptr_t ClassDescLookup = Confirmed::ClassDescLookup;
            inline constexpr std::uintptr_t SetCreator = Confirmed::SetCreator;
            inline constexpr std::uintptr_t InstanceSetParent = Confirmed::InstanceSetParent;
        }
    }

    namespace FakeDataModel {
        inline constexpr std::uintptr_t Pointer = 0x8C426F8;
    }

    namespace FastClusterEntity {
        inline constexpr std::uintptr_t VTableRva = 0x68cda58;
    }

    namespace Highlight {
        inline constexpr std::uintptr_t InvalidateAdornee = 0x930380;
        inline constexpr std::uintptr_t SetAdornee = 0x21758d0;
    }

    namespace Instance {
        inline constexpr std::uintptr_t ClassByName = 0xd65fc0;
        inline constexpr std::uintptr_t CreatorFromClass = 0x8ec9b0;
        inline constexpr std::uintptr_t FromExisting = 0x232d7d0;
        inline constexpr std::uintptr_t New = 0x232ca10;
        inline constexpr std::uintptr_t PushToLua = 0x7c5820;
        inline constexpr std::uintptr_t SetParent = 0x8b6d70;
        inline constexpr std::uintptr_t WhJobVftable = 0x697e748;
    }

    namespace Luau {
        inline constexpr std::uintptr_t EmptyNode = 0x610b760;
        inline constexpr std::uintptr_t collectgarbage_wrap = 0x2414880;
        inline constexpr std::uintptr_t index2addr = 0x937da0;
        inline constexpr std::uintptr_t loadstring = 0x2415900;
        inline constexpr std::uintptr_t lua_gc = 0x93a040;
        inline constexpr std::uintptr_t lua_getglobal = 0x0;
        inline constexpr std::uintptr_t lua_pushinteger = 0x938e80;
        inline constexpr std::uintptr_t lua_pushlightuserdata = 0x939140;
        inline constexpr std::uintptr_t lua_pushnil = 0x9392c0;
        inline constexpr std::uintptr_t lua_pushobject = 0x938ef0;
        inline constexpr std::uintptr_t lua_pushvalue = 0x937d30;
        inline constexpr std::uintptr_t luaC_fullgc = 0x94c250;
        inline constexpr std::uintptr_t luaC_step = 0x94bec0;
        inline constexpr std::uintptr_t luaC_step_work = 0x94bbf0;
        inline constexpr std::uintptr_t luaL_getmetafield = 0x93da80;
        inline constexpr std::uintptr_t print_wrap = 0x24165b0;
        inline constexpr std::uintptr_t require = 0x24166c0;
        inline constexpr std::uintptr_t require_impl = 0x0;
    }

    namespace LuauGlobal {
        inline constexpr std::uintptr_t dummynode = 0x610b760;
    }

    namespace Reflection {
        inline constexpr std::uintptr_t CreatorTable = 0x8690b58;
        inline constexpr std::uintptr_t NameRegistry = 0x83700d8;
        inline constexpr std::uintptr_t DescriptorNativeOffset = 0x80;

        namespace Raycast {
            inline constexpr std::uintptr_t Descriptor = 0x81e7150;
            inline constexpr std::uintptr_t Native = 0x3be2f40;
            inline constexpr bool PackedRay = false;
        }

        namespace FindPartOnRay {
            inline constexpr std::uintptr_t Descriptor = 0x81e77f0;
            inline constexpr std::uintptr_t Native = 0x3bd83f0;
            inline constexpr bool PackedRay = true;
        }

        namespace FindPartOnRayWithIgnoreList {
            inline constexpr std::uintptr_t Descriptor = 0x81e78a0;
            inline constexpr std::uintptr_t Native = 0x3bd8320;
            inline constexpr bool PackedRay = true;
        }

        namespace FindPartOnRayWithWhitelist {
            inline constexpr std::uintptr_t Descriptor = 0x81e7950;
            inline constexpr std::uintptr_t Native = 0x3bd8250;
            inline constexpr bool PackedRay = true;
        }

        namespace FindPartOnRayLowercase {
            inline constexpr std::uintptr_t Descriptor = 0x81e7cf0;
            inline constexpr std::uintptr_t Native = 0x3bd83f0;
            inline constexpr bool PackedRay = true;
        }
    }

    namespace TaskScheduler {
        inline constexpr std::uintptr_t Pointer = 0x89DD108;
    }

    namespace VisualEngine {
        inline constexpr std::uintptr_t Pointer = 0x827DD88;
    }

    namespace WorldRoot {
        inline constexpr std::uintptr_t RaycastBoundDesc = 0x8089f20;
    }
}