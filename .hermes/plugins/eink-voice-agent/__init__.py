from . import schemas, tools


def register(ctx):
    ctx.register_tool(
        name="build_firmware",
        toolset="eink_voice_agent",
        schema=schemas.BUILD_FIRMWARE,
        handler=tools.build_firmware,
    )
    ctx.register_tool(
        name="flash_firmware",
        toolset="eink_voice_agent",
        schema=schemas.FLASH_FIRMWARE,
        handler=tools.flash_firmware,
    )
    ctx.register_tool(
        name="monitor_device",
        toolset="eink_voice_agent",
        schema=schemas.MONITOR_DEVICE,
        handler=tools.monitor_device,
    )
    ctx.register_tool(
        name="ci_status",
        toolset="eink_voice_agent",
        schema=schemas.CI_STATUS,
        handler=tools.ci_status,
    )
