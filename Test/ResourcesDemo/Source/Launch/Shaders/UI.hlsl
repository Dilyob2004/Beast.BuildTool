struct VertexInput
{
    [[vk::location(0)]] float2 position : POSITION;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
    [[vk::location(2)]] float4 color : COLOR0;
};
struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
};
FragmentInput VSMain(VertexInput input)
{
    FragmentInput output;
    output.position = float4(input.position, 0, 1);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
[[vk::binding(0, 0)]] Texture2D<float4> ColorTexture;
[[vk::binding(1, 0)]] SamplerState ColorSampler;
float4 PSMain(FragmentInput input) : SV_Target0
{
    return ColorTexture.Sample(ColorSampler, input.uv) * input.color;
}
