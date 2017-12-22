/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2016-2018 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#pragma once

#include <algorithm>
#include "api/replay/renderdoc_replay.h"
#include "common/wrapped_pool.h"
#include "core/core.h"
#include "core/resource_manager.h"
#include "driver/d3d12/d3d12_common.h"
#include "serialise/serialiser.h"

enum D3D12ResourceType
{
  Resource_Unknown = 0,
  Resource_Device,
  Resource_CommandAllocator,
  Resource_CommandQueue,
  Resource_CommandSignature,
  Resource_DescriptorHeap,
  Resource_Fence,
  Resource_Heap,
  Resource_PipelineState,
  Resource_QueryHeap,
  Resource_Resource,
  Resource_GraphicsCommandList,
  Resource_RootSignature,
  Resource_PipelineLibrary,
};

DECLARE_REFLECTION_ENUM(D3D12ResourceType);

class WrappedID3D12DescriptorHeap;

// squeeze the descriptor a bit so that the below struct fits in 64 bytes
struct D3D12_UNORDERED_ACCESS_VIEW_DESC_SQUEEZED
{
  // pull up and compress down to 1 byte the enums/flags that don't have any larger values
  uint8_t Format;
  uint8_t ViewDimension;
  uint8_t BufferFlags;

  // 5 more bytes here - below union is 8-byte aligned

  union
  {
    struct D3D12_BUFFER_UAV_SQUEEZED
    {
      UINT64 FirstElement;
      UINT NumElements;
      UINT StructureByteStride;
      UINT64 CounterOffsetInBytes;
    } Buffer;
    D3D12_TEX1D_UAV Texture1D;
    D3D12_TEX1D_ARRAY_UAV Texture1DArray;
    D3D12_TEX2D_UAV Texture2D;
    D3D12_TEX2D_ARRAY_UAV Texture2DArray;
    D3D12_TEX3D_UAV Texture3D;
  };

  void Init(const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc)
  {
    Format = (uint8_t)desc.Format;
    ViewDimension = (uint8_t)desc.ViewDimension;

    // all but buffer elements should fit in 4 UINTs, so we can copy the Buffer (minus the flags we
    // moved) and still cover them.
    RDCCOMPILE_ASSERT(sizeof(Texture1D) <= 4 * sizeof(UINT), "Buffer isn't largest union member!");
    RDCCOMPILE_ASSERT(sizeof(Texture1DArray) <= 4 * sizeof(UINT),
                      "Buffer isn't largest union member!");
    RDCCOMPILE_ASSERT(sizeof(Texture2D) <= 4 * sizeof(UINT), "Buffer isn't largest union member!");
    RDCCOMPILE_ASSERT(sizeof(Texture2DArray) <= 4 * sizeof(UINT),
                      "Buffer isn't largest union member!");
    RDCCOMPILE_ASSERT(sizeof(Texture3D) <= 4 * sizeof(UINT), "Buffer isn't largest union member!");

    Buffer.FirstElement = desc.Buffer.FirstElement;
    Buffer.NumElements = desc.Buffer.NumElements;
    Buffer.StructureByteStride = desc.Buffer.StructureByteStride;
    Buffer.CounterOffsetInBytes = desc.Buffer.CounterOffsetInBytes;
    BufferFlags = (uint8_t)desc.Buffer.Flags;
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC AsDesc() const
  {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};

    desc.Format = (DXGI_FORMAT)Format;
    desc.ViewDimension = (D3D12_UAV_DIMENSION)ViewDimension;

    desc.Buffer.FirstElement = Buffer.FirstElement;
    desc.Buffer.NumElements = Buffer.NumElements;
    desc.Buffer.StructureByteStride = Buffer.StructureByteStride;
    desc.Buffer.CounterOffsetInBytes = Buffer.CounterOffsetInBytes;
    desc.Buffer.Flags = (D3D12_BUFFER_UAV_FLAGS)BufferFlags;

    return desc;
  }
};

enum class D3D12DescriptorType
{
  // we start at 0x1000 since this element will alias with the filter
  // in the sampler, to save space
  Sampler,
  CBV = 0x1000,
  SRV,
  UAV,
  RTV,
  DSV,
  Undefined,
};

DECLARE_REFLECTION_ENUM(D3D12DescriptorType);

struct D3D12Descriptor
{
  D3D12DescriptorType GetType() const
  {
    RDCCOMPILE_ASSERT(sizeof(D3D12Descriptor) <= 64, "D3D12Descriptor has gotten larger");

    if(nonsamp.type < D3D12DescriptorType::CBV)
      return D3D12DescriptorType::Sampler;

    return nonsamp.type;
  }

  operator D3D12_CPU_DESCRIPTOR_HANDLE() const
  {
    D3D12_CPU_DESCRIPTOR_HANDLE handle;
    handle.ptr = (SIZE_T) this;
    return handle;
  }

  operator D3D12_GPU_DESCRIPTOR_HANDLE() const
  {
    D3D12_GPU_DESCRIPTOR_HANDLE handle;
    handle.ptr = (SIZE_T) this;
    return handle;
  }

  void Init(const D3D12_SAMPLER_DESC *pDesc);
  void Init(const D3D12_CONSTANT_BUFFER_VIEW_DESC *pDesc);
  void Init(ID3D12Resource *pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC *pDesc);
  void Init(ID3D12Resource *pResource, ID3D12Resource *pCounterResource,
            const D3D12_UNORDERED_ACCESS_VIEW_DESC *pDesc);
  void Init(ID3D12Resource *pResource, const D3D12_RENDER_TARGET_VIEW_DESC *pDesc);
  void Init(ID3D12Resource *pResource, const D3D12_DEPTH_STENCIL_VIEW_DESC *pDesc);

  void Create(D3D12_DESCRIPTOR_HEAP_TYPE heapType, WrappedID3D12Device *dev,
              D3D12_CPU_DESCRIPTOR_HANDLE handle);
  void CopyFrom(const D3D12Descriptor &src);
  void GetRefIDs(ResourceId &id, ResourceId &id2, FrameRefType &ref);

  union
  {
    // keep the sampler outside as it's the largest descriptor
    struct
    {
      // same location in both structs
      WrappedID3D12DescriptorHeap *heap;
      uint32_t idx;

      D3D12_SAMPLER_DESC desc;
    } samp;

    struct
    {
      // same location in both structs
      WrappedID3D12DescriptorHeap *heap;
      uint32_t idx;

      // this element overlaps with the D3D12_FILTER in D3D12_SAMPLER_DESC,
      // with values that are invalid for filter
      D3D12DescriptorType type;

      ID3D12Resource *resource;

      union
      {
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv;
        struct
        {
          ID3D12Resource *counterResource;
          D3D12_UNORDERED_ACCESS_VIEW_DESC_SQUEEZED desc;
        } uav;
        D3D12_RENDER_TARGET_VIEW_DESC rtv;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv;
      };
    } nonsamp;
  };
};

DECLARE_REFLECTION_STRUCT(D3D12Descriptor);

inline D3D12Descriptor *GetWrapped(D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
  return (D3D12Descriptor *)handle.ptr;
}

inline D3D12Descriptor *GetWrapped(D3D12_GPU_DESCRIPTOR_HANDLE handle)
{
  return (D3D12Descriptor *)handle.ptr;
}

D3D12_CPU_DESCRIPTOR_HANDLE Unwrap(D3D12_CPU_DESCRIPTOR_HANDLE handle);
D3D12_GPU_DESCRIPTOR_HANDLE Unwrap(D3D12_GPU_DESCRIPTOR_HANDLE handle);
D3D12_CPU_DESCRIPTOR_HANDLE UnwrapCPU(D3D12Descriptor *handle);
D3D12_GPU_DESCRIPTOR_HANDLE UnwrapGPU(D3D12Descriptor *handle);

struct PortableHandle
{
  PortableHandle() : index(0) {}
  PortableHandle(ResourceId id, uint32_t i) : heap(id), index(i) {}
  PortableHandle(uint32_t i) : index(i) {}
  ResourceId heap;
  uint32_t index;
};

DECLARE_REFLECTION_STRUCT(PortableHandle);

class D3D12ResourceManager;

PortableHandle ToPortableHandle(D3D12Descriptor *handle);
PortableHandle ToPortableHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle);
PortableHandle ToPortableHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle);
D3D12_CPU_DESCRIPTOR_HANDLE CPUHandleFromPortableHandle(D3D12ResourceManager *manager,
                                                        PortableHandle handle);
D3D12_GPU_DESCRIPTOR_HANDLE GPUHandleFromPortableHandle(D3D12ResourceManager *manager,
                                                        PortableHandle handle);
D3D12Descriptor *DescriptorFromPortableHandle(D3D12ResourceManager *manager, PortableHandle handle);

struct DynamicDescriptorWrite
{
  D3D12Descriptor desc;

  D3D12Descriptor *dest;
};

struct DynamicDescriptorCopy
{
  DynamicDescriptorCopy() : dst(NULL), src(NULL), type(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {}
  DynamicDescriptorCopy(D3D12Descriptor *d, D3D12Descriptor *s, D3D12_DESCRIPTOR_HEAP_TYPE t)
      : dst(d), src(s), type(t)
  {
  }

  D3D12Descriptor *dst;
  D3D12Descriptor *src;
  D3D12_DESCRIPTOR_HEAP_TYPE type;
};

DECLARE_REFLECTION_STRUCT(DynamicDescriptorCopy);

struct D3D12ResourceRecord;

struct TileMapping
{
  // this is mappings into a single heap
  // a single resource can have more than one of these
  // because ReservedResources can point to multiple heaps

  // resource stuff
  UINT NumResourceRegions;

  // D3D12_TILED_RESOURCE_COORDINATE pResourceRegionStartCoordinates[NumResourceRegions];
  //vector<D3D12_TILED_RESOURCE_COORDINATE> resourceRegionStartCoords;
  D3D12_TILED_RESOURCE_COORDINATE* resourceRegionStartCoords;

  // D3D12_TILE_REGION_SIZE pResourceRegionSizes[NumResourceRegions];
  //vector<D3D12_TILE_REGION_SIZE> resourceRegionSizes;
  D3D12_TILE_REGION_SIZE* resourceRegionSizes;

  // heap stuff
  //ID3D12Heap *pHeap; //needed? make a hash where heap ptr is key and this struct is value

  UINT NumRanges;

  // D3D12_TILE_RANGE_FLAGS pRangeFlags[NumRanges];
  //vector<D3D12_TILE_RANGE_FLAGS> rangeFlags;
  D3D12_TILE_RANGE_FLAGS* rangeFlags;

  // UINT pHeapRangeStartOffsets[NumRanges];
  //vector<UINT> heapRangeStarts;
  UINT* heapRangeStarts;

  // UINT pRangeTileCounts[NumRanges];
  //vector<UINT> rangeTileCounts;
  UINT* rangeTileCounts;

  //flags for this mapping (only supported one is D3D12_TILE_MAPPING_FLAG_NONE so [shrug]
  D3D12_TILE_MAPPING_FLAGS Flags;

  /* 
  ID3D12Resource *pResource, 
  UINT NumResourceRegions,
    const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
    const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges,
    const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets,
    const UINT *pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags
  
  Clearing an entire surface's mappings to NULL
  // - NULL for pResourceRegionStartCoordinates and pResourceRegionSizes defaults to the entire resource
// - NULL for pHeapRangeStartOffsets since it isn't needed for mapping tiles to NULL
// - NULL for pRangeTileCounts when NumRanges is 1 defaults to the same number of tiles as the resource region (which is
//   the entire surface in this case)
//
UINT RangeFlags = D3D12_TILE_RANGE_FLAG_NULL;
pCommandQueue->UpdateTileMappings(pResource, 1, NULL, NULL, NULL, 1, &RangeFlags, NULL, NULL, D3D12_TILE_MAPPING_FLAG_NONE);
  
  */
  
  //init to null mappings for arg-less constructor
  TileMapping() : 
	  NumResourceRegions(1), resourceRegionStartCoords(NULL), resourceRegionSizes(NULL),
	  NumRanges(1), rangeFlags(NULL), heapRangeStarts(NULL), rangeTileCounts(NULL), Flags(D3D12_TILE_MAPPING_FLAG_NONE)
  {}

  TileMapping(UINT inNumResourceRegions, const D3D12_TILED_RESOURCE_COORDINATE* inResourceRegionStartCoordinates,
      const D3D12_TILE_REGION_SIZE* inResourceRegionSizes, UINT inNumRanges,
      const D3D12_TILE_RANGE_FLAGS* inRangeFlags, const UINT* inHeapRangeStartOffsets,
      const UINT* inRangeTileCounts, D3D12_TILE_MAPPING_FLAGS inFlags=D3D12_TILE_MAPPING_FLAG_NONE)
      : NumResourceRegions(inNumResourceRegions), NumRanges(inNumRanges), Flags(inFlags)
  {
      resourceRegionStartCoords = new D3D12_TILED_RESOURCE_COORDINATE[NumResourceRegions];
      resourceRegionSizes = new D3D12_TILE_REGION_SIZE[NumResourceRegions];

	  memcpy(resourceRegionStartCoords, inResourceRegionStartCoordinates, sizeof(D3D12_TILED_RESOURCE_COORDINATE) * NumResourceRegions);
	  memcpy(resourceRegionSizes, inResourceRegionSizes, sizeof(D3D12_TILE_REGION_SIZE) * NumResourceRegions);

	  rangeFlags = new D3D12_TILE_RANGE_FLAGS[NumRanges];
	  heapRangeStarts = new UINT[NumRanges];
	  rangeTileCounts = new UINT[NumRanges];

	  memcpy(rangeFlags, inRangeFlags, sizeof(D3D12_TILE_RANGE_FLAGS) * NumRanges);
	  memcpy(heapRangeStarts, inHeapRangeStartOffsets, sizeof(UINT) * NumRanges);
	  memcpy(rangeTileCounts, inRangeTileCounts, sizeof(UINT) * NumRanges);
  }

  ~TileMapping()
  {
	  delete resourceRegionStartCoords;
	  delete resourceRegionSizes;
	  delete rangeFlags;
	  delete heapRangeStarts;
	  delete rangeTileCounts;
  }
};

// this is for managing our own memory for tiled/reserved resources
struct ReservedResource
{
  // tile size in pixels (width x height x depth)
  // get from GetResourceTiling()
  D3D12_TILE_SHAPE tileSize;

  D3D12_RESOURCE_DESC desc;
  D3D12_RESOURCE_STATES state;
  D3D12_CLEAR_VALUE clearVal;

  //set<TileMapping *> mappings;
  map<ID3D12Heap*, TileMapping&> mappings;

  ReservedResource(){}
  ReservedResource(D3D12_RESOURCE_DESC inDesc, D3D12_RESOURCE_STATES inState,
                   D3D12_CLEAR_VALUE inClearVal)
      : desc(inDesc),
        state(inState),
        clearVal(inClearVal)
  {
    // init the mappings with null mappings?
  }

  void Update(UINT NumResourceRegions,
              const D3D12_TILED_RESOURCE_COORDINATE *pResourceRegionStartCoordinates,
              const D3D12_TILE_REGION_SIZE *pResourceRegionSizes, ID3D12Heap *pHeap, UINT NumRanges,
              const D3D12_TILE_RANGE_FLAGS *pRangeFlags, const UINT *pHeapRangeStartOffsets,
              const UINT *pRangeTileCounts, D3D12_TILE_MAPPING_FLAGS Flags)
  {
    // this gets called when we use UpdateTileMappings.

    // check the inputs to see if we're updating to non-null mappings

    // if it is null, clear the mappings
    if (pHeap == NULL)
    {
      mappings.clear();
    }

    // probably should just destroy and recreate mappings on the same heap
    // heap pointer should be the key?

	// if we have start coords, but no region sizes
    if(pResourceRegionStartCoordinates != NULL && pResourceRegionSizes == NULL)
    {
      // the region size defaults to 1 tile for all regions.
    }

    // Each given tile range can specify one of a few types of ranges:
    // a range of tiles in a heap (default), (D3D12_TILE_RANGE_FLAG_NONE)
    // num reserved resource tiles to map to 1 heap tile (D3D12_TILE_RANGE_FLAG_REUSE_SINGLE_TILE)
    // # tile mappings in the reserved resource to skip/leave unchanged (D3D12_TILE_RANGE_FLAG_SKIP)
    // a count of tiles in the heap to map to NULL. (D3D12_TILE_RANGE_FLAG_NULL)
  }
};

struct CmdListRecordingInfo
{
  vector<D3D12_RESOURCE_BARRIER> barriers;

  // tiled resources referenced by this command buffer (at submit time
  // need to go through the tile mappings and reference all memory)
  set<ReservedResource *> tiledResources;

  // a list of all resources dirtied by this command list
  set<ResourceId> dirtied;

  // a list of descriptors that are bound at any point in this command list
  // used to look up all the frame refs per-descriptor and apply them on queue
  // submit with latest binding refs.
  // We allow duplicates in here since it's a better tradeoff to let the vector
  // expand a bit more to contain duplicates and then deal with it during frame
  // capture, than to constantly be deduplicating during record (e.g. with a
  // set or sorted vector).
  vector<D3D12Descriptor *> boundDescs;

  // bundles executed
  vector<D3D12ResourceRecord *> bundles;
};

class WrappedID3D12Resource;

struct GPUAddressRange
{
  D3D12_GPU_VIRTUAL_ADDRESS start, end;
  ResourceId id;

  bool operator<(const D3D12_GPU_VIRTUAL_ADDRESS &o) const
  {
    if(o < start)
      return true;

    return false;
  }
};

struct GPUAddressRangeTracker
{
  GPUAddressRangeTracker() {}
  // no copying
  GPUAddressRangeTracker(const GPUAddressRangeTracker &);
  GPUAddressRangeTracker &operator=(const GPUAddressRangeTracker &);

  std::vector<GPUAddressRange> addresses;
  Threading::CriticalSection addressLock;

  void AddTo(GPUAddressRange range)
  {
    SCOPED_LOCK(addressLock);
    auto it = std::lower_bound(addresses.begin(), addresses.end(), range.start);
    RDCASSERT(it == addresses.begin() || it == addresses.end() || range.start < it->start ||
              range.start >= it->end);

    addresses.insert(it, range);
  }

  void RemoveFrom(D3D12_GPU_VIRTUAL_ADDRESS baseAddr)
  {
    SCOPED_LOCK(addressLock);
    auto it = std::lower_bound(addresses.begin(), addresses.end(), baseAddr);
    RDCASSERT(it != addresses.end() && baseAddr >= it->start && baseAddr < it->end);

    addresses.erase(it);
  }

  void GetResIDFromAddr(D3D12_GPU_VIRTUAL_ADDRESS addr, ResourceId &id, UINT64 &offs)
  {
    id = ResourceId();
    offs = 0;

    if(addr == 0)
      return;

    GPUAddressRange range;

    // this should really be a read-write lock
    {
      SCOPED_LOCK(addressLock);

      auto it = std::lower_bound(addresses.begin(), addresses.end(), addr);
      if(it == addresses.end())
        return;

      range = *it;
    }

    if(addr < range.start || addr >= range.end)
      return;

    id = range.id;
    offs = addr - range.start;
  }
};

struct MapState
{
  ID3D12Resource *res;
  UINT subres;
  UINT64 totalSize;
};

struct D3D12ResourceRecord : public ResourceRecord
{
  enum
  {
    NullResource = NULL
  };

  D3D12ResourceRecord(ResourceId id)
      : ResourceRecord(id, true),
        type(Resource_Unknown),
        ContainsExecuteIndirect(false),
        cmdInfo(NULL),
        bakedCommands(NULL)
  {
  }
  ~D3D12ResourceRecord() { SAFE_DELETE(cmdInfo); }
  void Bake()
  {
    RDCASSERT(cmdInfo);
    SwapChunks(bakedCommands);
    cmdInfo->barriers.swap(bakedCommands->cmdInfo->barriers);
    cmdInfo->dirtied.swap(bakedCommands->cmdInfo->dirtied);
    cmdInfo->boundDescs.swap(bakedCommands->cmdInfo->boundDescs);
    cmdInfo->bundles.swap(bakedCommands->cmdInfo->bundles);
  }

  void Insert(map<int32_t, Chunk *> &recordlist)
  {
    bool dataWritten = DataWritten;

    DataWritten = true;

    for(auto it = Parents.begin(); it != Parents.end(); ++it)
    {
      if(!(*it)->DataWritten)
      {
        (*it)->Insert(recordlist);
      }
    }

    if(!dataWritten)
      recordlist.insert(m_Chunks.begin(), m_Chunks.end());
  }

  D3D12ResourceType type;
  bool ContainsExecuteIndirect;
  D3D12ResourceRecord *bakedCommands;
  CmdListRecordingInfo *cmdInfo;

  struct MapData
  {
    MapData() : refcount(0), realPtr(NULL), shadowPtr(NULL) {}
    volatile int32_t refcount;
    byte *realPtr;
    byte *shadowPtr;
  };

  vector<MapData> m_Map;
};

typedef vector<D3D12_RESOURCE_STATES> SubresourceStateVector;

struct D3D12InitialContents
{
  enum Tag
  {
    Copy,
    // this is only valid during capture - it indicates we didn't create a staging texture, we're
    // going to read directly from the resource (only valid for resources that are already READBACK)
    MapDirect,
    Multisampled,
  };
  D3D12InitialContents(D3D12Descriptor *d, uint32_t n)
      : tag(Copy),
        resourceType(Resource_DescriptorHeap),
        descriptors(d),
        numDescriptors(n),
        resource(NULL)
  {
  }
  D3D12InitialContents(ID3D12DescriptorHeap *r)
      : tag(Copy),
        resourceType(Resource_DescriptorHeap),
        descriptors(NULL),
        numDescriptors(0),
        resource(r)
  {
  }
  D3D12InitialContents(ID3D12Resource *r)
      : tag(Copy),
        resourceType(Resource_DescriptorHeap),
        descriptors(NULL),
        numDescriptors(0),
        resource(r)
  {
  }
  D3D12InitialContents(Tag tg)
      : tag(tg),
        resourceType(Resource_DescriptorHeap),
        descriptors(NULL),
        numDescriptors(0),
        resource(NULL)
  {
  }
  D3D12InitialContents()
      : tag(Copy), resourceType(Resource_Unknown), descriptors(NULL), numDescriptors(0), resource(NULL)
  {
  }
  template <typename Configuration>
  void Free(ResourceManager<Configuration> *rm)
  {
    SAFE_RELEASE(resource);
  }

  Tag tag;
  D3D12ResourceType resourceType;
  D3D12Descriptor *descriptors;
  uint32_t numDescriptors;
  ID3D12DeviceChild *resource;
};

struct D3D12ResourceManagerConfiguration
{
  typedef ID3D12DeviceChild *WrappedResourceType;
  typedef ID3D12DeviceChild *RealResourceType;
  typedef D3D12ResourceRecord RecordType;
  typedef D3D12InitialContents InitialContentData;
};

class D3D12ResourceManager : public ResourceManager<D3D12ResourceManagerConfiguration>
{
public:
  D3D12ResourceManager(CaptureState state, WrappedID3D12Device *dev)
      : ResourceManager(), m_State(state), m_Device(dev)
  {
  }
  template <class T>
  T *GetLiveAs(ResourceId id)
  {
    return (T *)GetLiveResource(id);
  }

  template <class T>
  T *GetCurrentAs(ResourceId id)
  {
    return (T *)GetCurrentResource(id);
  }

  void ApplyBarriers(vector<D3D12_RESOURCE_BARRIER> &barriers,
                     map<ResourceId, SubresourceStateVector> &states);

  template <typename SerialiserType>
  void SerialiseResourceStates(SerialiserType &ser, std::vector<D3D12_RESOURCE_BARRIER> &barriers,
                               std::map<ResourceId, SubresourceStateVector> &states);

  template <typename SerialiserType>
  bool Serialise_InitialState(SerialiserType &ser, ResourceId resid, ID3D12DeviceChild *res);

private:
  bool SerialisableResource(ResourceId id, D3D12ResourceRecord *record);
  ResourceId GetID(ID3D12DeviceChild *res);

  bool ResourceTypeRelease(ID3D12DeviceChild *res);

  bool Force_InitialState(ID3D12DeviceChild *res, bool prepare);
  bool Need_InitialStateChunk(ID3D12DeviceChild *res);
  bool Prepare_InitialState(ID3D12DeviceChild *res);
  uint32_t GetSize_InitialState(ResourceId id, ID3D12DeviceChild *res);
  bool Serialise_InitialState(WriteSerialiser &ser, ResourceId resid, ID3D12DeviceChild *res)
  {
    return Serialise_InitialState<WriteSerialiser>(ser, resid, res);
  }
  void Create_InitialState(ResourceId id, ID3D12DeviceChild *live, bool hasData);
  void Apply_InitialState(ID3D12DeviceChild *live, D3D12InitialContents data);

  CaptureState m_State;
  WrappedID3D12Device *m_Device;
};
