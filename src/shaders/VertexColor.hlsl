struct VertexIn
{
	float3 pos_local : POSITION;
	float3 normal	 : NORMAL;
	float2 uv		 : TEXCOORD;
	float3 tangent	 : TANGENT;
};

struct VertexOut
{
	float4 pos_sp : SV_POSITION;
	float4 color : COLOR;
};

#ifdef VERTEX_SHADER

cbuffer cbPerFrame : register(b0)
{
	float4x4 g_view_proj;
}

cbuffer cbPerObject : register(b1)
{
	float4x4 g_model;
};

VertexOut vs_main(VertexIn vsIn)
{
	VertexOut vsOut;

	float4x4 mvp = mul(g_view_proj, g_model);
	float4 pos_sp = mul(mvp, float4(vsIn.pos_local, 1.0f));

	vsOut.pos_sp = pos_sp;
	vsOut.color = float4(abs(vsIn.normal), 1.0f);

	return vsOut;
}

#endif // VERTEX_SHADER

#ifdef PIXEL_SHADER

float4 ps_main(VertexOut psIn) : SV_TARGET
{
	return psIn.color;
}

#endif // PIXEL_SHADER