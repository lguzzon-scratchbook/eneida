#include "demo01.h"
#include "library.h"
#include "math.h"
#include "windows.h"
#include "renderer.h"


#define k_triangle_count 8

typedef struct demo {
    renderer_t *renderer;
    ID3D12PipelineState *pso;
    ID3D12RootSignature *root_sig;
    ID3D12Resource *vertex_buffer;
    ID3D12Resource *constant_buffer;
    void *constant_buffer_cpu_addr;
} demo_t;

void demo_update(demo_t *demo, f64 frame_time, f32 frame_delta_time)
{
    (void)frame_delta_time;

    f32mat4 m0;
    f32mat4_rotation_ay(&m0, (f32)frame_time);

    f32mat4 m1;
    /* look at matrix */ {
        f32vec3 eye, at, up;
        f32mat4_look_at(&m1, f32vec3_set(&eye, 2.0f, 2.0f, -2.0f), f32vec3_set(&at, 0.0f, 0.0f, 0.0f),
                        f32vec3_set(&up, 0.0f, 1.0f, 0.0f));
    }

    f32mat4 m2;
    f32mat4_fovperspective(&m2, k_1pi_div_4, 1.777f, 0.1f, 20.0f);

    f32mat4_mul(&m0, &m0, &m1);
    f32mat4_mul(&m0, &m0, &m2);

    *(f32mat4 *)demo->constant_buffer_cpu_addr = *f32mat4_transpose(&m0, &m0);
}

void demo_draw(demo_t *demo)
{
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (f32)k_win_width, (f32)k_win_height, 0.0f, 1.0f };
    D3D12_RECT scissor = { 0, 0, k_win_width, k_win_height };

    ID3D12CommandAllocator *cmdalloc = demo->renderer->cmdalloc[demo->renderer->frame_index];
    ID3D12CommandAllocator_Reset(cmdalloc);

    ID3D12GraphicsCommandList *cmdlist = demo->renderer->cmdlist;

    ID3D12GraphicsCommandList_Reset(cmdlist, cmdalloc, NULL);
    ID3D12GraphicsCommandList_RSSetViewports(cmdlist, 1, &viewport);
    ID3D12GraphicsCommandList_RSSetScissorRects(cmdlist, 1, &scissor);

    u32 back_buffer_index = demo->renderer->back_buffer_index;
    D3D12_RESOURCE_BARRIER barrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Transition.pResource = demo->renderer->color_buffers[back_buffer_index],
        .Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT,
        .Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
    };
    ID3D12GraphicsCommandList_ResourceBarrier(cmdlist, 1, &barrier);

    D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = demo->renderer->dsv_heap_start;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = demo->renderer->rtv_heap_start;
    rtv_handle.ptr += back_buffer_index * demo->renderer->descriptor_size_rtv;

    ID3D12GraphicsCommandList_OMSetRenderTargets(cmdlist, 1, &rtv_handle, 0, &dsv_handle);

    f32 clear_color[] = { 0.0f, 0.2f, 0.4f, 1.0f };
    ID3D12GraphicsCommandList_ClearRenderTargetView(cmdlist, rtv_handle, clear_color, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(cmdlist, dsv_handle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

    ID3D12GraphicsCommandList_SetPipelineState(cmdlist, demo->pso);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(cmdlist, demo->root_sig);

    ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(cmdlist, 0,
                                                                ID3D12Resource_GetGPUVirtualAddress(demo->constant_buffer));
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(cmdlist, D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);

    D3D12_VERTEX_BUFFER_VIEW vb_view = {
        .BufferLocation = ID3D12Resource_GetGPUVirtualAddress(demo->vertex_buffer),
        .SizeInBytes = k_triangle_count * 4 * sizeof(f32vec3),
        .StrideInBytes = sizeof(f32vec3)
    };
    ID3D12GraphicsCommandList_IASetVertexBuffers(cmdlist, 0, 1, &vb_view);
    for (i32 i = 0; i < k_triangle_count; ++i)
        ID3D12GraphicsCommandList_DrawInstanced(cmdlist, 4, 1, i * 4, 0);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    ID3D12GraphicsCommandList_ResourceBarrier(cmdlist, 1, &barrier);

    ID3D12GraphicsCommandList_Close(cmdlist);
    ID3D12CommandQueue_ExecuteCommandLists(demo->renderer->cmdqueue, 1, (ID3D12CommandList **)&cmdlist);

    gr_present_frame(demo->renderer);
}

demo_t *demo_init(void *window)
{
    static demo_t demo_storage;

    demo_t *demo = &demo_storage;
    demo->renderer = gr_init(window);

    ID3D12Device *d3d = demo->renderer->d3d;

    /* pso */ {
        extern u8 vs_triangle[], ps_triangle[];
        extern u32 vs_triangle_size, ps_triangle_size;

        D3D12_INPUT_ELEMENT_DESC input_layout_desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {
            .InputLayout = { input_layout_desc, 1 },
            .VS = { vs_triangle, vs_triangle_size },
            .PS = { ps_triangle, ps_triangle_size },
            .RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME,
            .RasterizerState.CullMode = D3D12_CULL_MODE_NONE,
            .RasterizerState.AntialiasedLineEnable = 1,
            .BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
            .SampleMask = 0xffffffff,
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE,
            .NumRenderTargets = 1,
            .RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM,
            .SampleDesc.Count = 1,
        };
        VHR(ID3D12Device_CreateGraphicsPipelineState(d3d, &pso_desc, &IID_ID3D12PipelineState, &demo->pso));
        VHR(ID3D12Device_CreateRootSignature(d3d, 0, vs_triangle, vs_triangle_size,
                                             &IID_ID3D12RootSignature, &demo->root_sig));
    }
    /* vertex buffer */ {
        demo->vertex_buffer = d3d_create_buffer(d3d, D3D12_HEAP_TYPE_UPLOAD, k_triangle_count * 4 * sizeof(f32vec3));

        D3D12_RANGE range = { 0 };
        f32 *ptr;
        f32 size = 0.7f;
        VHR(ID3D12Resource_Map(demo->vertex_buffer, 0, &range, &ptr));
        for (u32 i = 0; i < k_triangle_count; ++i) {
            *ptr++ = -size; *ptr++ = -size; *ptr++ = 0.0f;
            *ptr++ = size; *ptr++ = -size; *ptr++ = 0.0f;
            *ptr++ = 0.0f; *ptr++ = size; *ptr++ = 0.0f;
            *ptr++ = -size; *ptr++ = -size; *ptr++ = 0.0f;
            size -= 0.1f;
        }
        ID3D12Resource_Unmap(demo->vertex_buffer, 0, NULL);
    }
    /* constant buffer */ {
        demo->constant_buffer = d3d_create_buffer(d3d, D3D12_HEAP_TYPE_UPLOAD, sizeof(f32mat4));

        D3D12_RANGE range = { 0 };
        VHR(ID3D12Resource_Map(demo->constant_buffer, 0, &range, &demo->constant_buffer_cpu_addr));
    }

#if 0
    i32 **array = NULL;
    char buffer[64];
    for (u32 i = 0; i < 128; ++i) {
        i32 *item = (i32 *)mem_alloc(sizeof(i32));
        array_add(array, item);
        wsprintf(buffer, "%p     (%u / %u)\n", array[i], array_size(array), array_capacity(array));
        OutputDebugString(buffer);
    }
#else
    typedef struct my_test_type {
        u32 x;
        u32 y;
    } my_test_type_t;

    my_test_type_t *array = NULL;
    char buffer[64];
    for (u32 i = 0; i < 128; ++i) {
        my_test_type_t s;
        s.x = i;
        s.y = i + 1;
        array_add(array, s);
        wsprintf(buffer, "%u     (%u / %u)\n", array[i].x, array_size(array), array_capacity(array));
        OutputDebugString(buffer);
    }
#endif

    return demo;
}

void demo_shutdown(demo_t *demo)
{
    gr_flush(demo->renderer);
    COMRELEASE(demo->pso);
    COMRELEASE(demo->root_sig);
    COMRELEASE(demo->vertex_buffer);
    COMRELEASE(demo->constant_buffer);
    gr_shutdown(demo->renderer);
}
