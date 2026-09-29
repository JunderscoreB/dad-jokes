#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path
import os
import re

top = '.'
out = 'build'

def generate_jokes_assets():
    """
    Parses the JavaScript joke array and generates the C header and raw text
    resource for the watchapp to use, keeping both environments synchronized.
    """
    js_path = 'src/pkjs/jokes.js' if os.path.exists('src/pkjs/jokes.js') else 'jokes.js'

    if not os.path.exists(js_path):
        return

    with open(js_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Extract strings from the JS array
    match = re.search(r'module\.exports\s*=\s*\[(.*?)\];', content, re.DOTALL)
    if not match:
        return

    jokes = re.findall(r'"(.*?)"(?=\s*,|\s*$)', match.group(1).strip(), re.DOTALL)

    # Ensure output directories exist
    os.makedirs('resources', exist_ok=True)
    os.makedirs('src/c', exist_ok=True)

    # Generate the raw text asset for the watch
    with open('resources/jokes.txt', 'w', encoding='utf-8') as f:
        for j in jokes:
            f.write(j.replace('\\"', '"').replace('\\n', '\n') + '\n')

    # Generate the header file for the C code
    with open('src/c/jokes.h', 'w', encoding='utf-8') as f:
        f.write("/* Auto-generated during pebble build - DO NOT EDIT */\n")
        f.write("#pragma once\n\n")
        f.write(f"#define NUM_BUILT_IN_JOKES {len(jokes)}\n")


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


def build(ctx):
    # Auto-generate our C assets from JS before the compiler runs
    generate_jokes_assets()

    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app')

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
