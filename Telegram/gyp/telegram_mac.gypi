# This file is part of Telegram Desktop,
# the official desktop version of Telegram messaging app, see https://telegram.org
#
# Telegram Desktop is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# It is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# In addition, as a special exception, the copyright holders give permission
# to link the code of portions of this program with the OpenSSL library.
#
# Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
# Copyright (c) 2014 John Preston, https://desktop.telegram.org

{
  'conditions': [[ 'build_mac', {
    'xcode_settings': {
      'GCC_PREFIX_HEADER': '<(src_loc)/stdafx.h',
      'GCC_PRECOMPILE_PREFIX_HEADER': 'YES',
      'INFOPLIST_FILE': '../Telegram.plist',
      'CURRENT_PROJECT_VERSION': '<!(./print_version.sh)',
      'ASSETCATALOG_COMPILER_APPICON_NAME': 'AppIcon',
      'OTHER_LDFLAGS': [
        '-lcups',
        '-lbsm',
        '-lm',
        '-lssl',
        '-lcrypto',
        '/usr/local/lib/liblzma.a',
        '/usr/local/lib/libopus.a',
        '/usr/local/lib/libexif.a',
      ],
    },
    'include_dirs': [
      '/usr/local/include',
      '<(libs_loc)/openssl-xcode/include'
    ],
    'library_dirs': [
      '/usr/local/lib',
      '<(libs_loc)/libexif-0.6.20/libexif/.libs',
      '<(libs_loc)/openssl-xcode',
    ],
    'configurations': {
      'Debug': {
        'xcode_settings': {
          'GCC_OPTIMIZATION_LEVEL': '0',
        },
      },
      'Release': {
        'xcode_settings': {
          'DEBUG_INFORMATION_FORMAT': 'dwarf-with-dsym',
          'LLVM_LTO': 'YES',
          'GCC_OPTIMIZATION_LEVEL': 'fast',
        },
      },
    },
    'mac_bundle': '1',
    'mac_bundle_resources': [
      '<!@(python -c "for s in \'<@(langpacks)\'.split(\' \'): print(\'<(res_loc)/langs/\' + s + \'.lproj/Localizable.strings\')")',
      '../Telegram/Images.xcassets'
    ],
  }], [ 'build_macold', {
    'xcode_settings': {
      'PRODUCT_BUNDLE_IDENTIFIER': 'com.tdesktop.Telegram',
      'OTHER_LDFLAGS': [
        '/usr/local/openal_old/lib/libopenal.a',
        '/usr/local/zlib_old/lib/libz.a',
        '/usr/local/iconv_old/lib/libiconv.a',
        '/usr/local/ffmpeg_old/lib/libavcodec.a',
        '/usr/local/ffmpeg_old/lib/libavformat.a',
        '/usr/local/ffmpeg_old/lib/libavutil.a',
        '/usr/local/ffmpeg_old/lib/libswscale.a',
        '/usr/local/ffmpeg_old/lib/libswresample.a',
        '-lbase',
        '-lcrashpad_client',
        '-lcrashpad_util',
      ],
    },
    'include_dirs': [
      '<(libs_loc)/crashpad_oldmac/crashpad',
      '<(libs_loc)/crashpad_oldmac/crashpad/third_party/mini_chromium/mini_chromium',
    ],
    'configurations': {
      'Debug': {
        'library_dirs': [
          '<(libs_loc)/crashpad_oldmac/crashpad/out/Debug',
        ],
      },
      'Release': {
        'library_dirs': [
          '<(libs_loc)/crashpad_oldmac/crashpad/out/Release',
        ],
      },
    },
    'postbuilds': [{
      'postbuild_name': 'Force Frameworks path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Frameworks/'
      ],
    }, {
      'postbuild_name': 'Copy Updater to Frameworks',
      'action': [
        'cp',
        '${BUILT_PRODUCTS_DIR}/Updater',
        '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Frameworks/',
      ],
    }, {
      'postbuild_name': 'Force Helpers path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Helpers/'
      ],
    }, {
      'postbuild_name': 'Copy crashpad_handler to Helpers',
      'action': [
        'cp',
        '<(libs_loc)/crashpad_oldmac/crashpad/out/${CONFIGURATION}/crashpad_handler',
        '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Helpers/',
      ],
    }],
  }, {
    'xcode_settings': {
      'OTHER_LDFLAGS': [
        '/usr/local/lib/libz.a',
        '/usr/local/lib/libopenal.a',
        '/usr/local/lib/libiconv.a',
        '/usr/local/lib/libavcodec.a',
        '/usr/local/lib/libavformat.a',
        '/usr/local/lib/libavutil.a',
        '/usr/local/lib/libswscale.a',
        '/usr/local/lib/libswresample.a',
      ],
    },
    'include_dirs': [
      '<(libs_loc)/crashpad/crashpad',
      '<(libs_loc)/crashpad/crashpad/third_party/mini_chromium/mini_chromium',
    ],
    'configurations': {
      'Debug': {
        'library_dirs': [
          '<(libs_loc)/crashpad/crashpad/out/Debug',
        ],
      },
      'Release': {
        'library_dirs': [
          '<(libs_loc)/crashpad/crashpad/out/Release',
        ],
      },
    },
  }], [ '"<(build_macold)" != "1" and "<(build_macstore)" != "1"', {
    'xcode_settings': {
      'PRODUCT_BUNDLE_IDENTIFIER': 'com.tdesktop.Telegram',
      'OTHER_LDFLAGS': [
        '-lbase',
        '-lcrashpad_client',
        '-lcrashpad_util',
      ],
     },
    'postbuilds': [{
      'postbuild_name': 'Force Frameworks path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Frameworks/'
      ],
    }, {
      'postbuild_name': 'Copy Updater to Frameworks',
      'action': [
        'cp',
        '${BUILT_PRODUCTS_DIR}/Updater',
        '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Frameworks/',
      ],
    }, {
      'postbuild_name': 'Force Helpers path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Helpers/'
      ],
    }, {
      'postbuild_name': 'Copy crashpad_client to Helpers',
      'action': [
        'cp',
        '<(libs_loc)/crashpad/crashpad/out/${CONFIGURATION}/crashpad_handler',
        '${BUILT_PRODUCTS_DIR}/Telegram.app/Contents/Helpers/',
      ],
    }],
  }], [ 'build_macstore', {
    'xcode_settings': {
      'PRODUCT_BUNDLE_IDENTIFIER': 'org.telegram.desktop',
      'OTHER_LDFLAGS': [
        '-framework', 'Breakpad',
      ],
      'FRAMEWORK_SEARCH_PATHS': [
        '<(libs_loc)/breakpad/src/client/mac/build/Release',
      ],
    },
    'mac_sandbox': 1,
    'mac_sandbox_development_team': '6N38VWS5BX',
    'product_name': 'Telegram Desktop',
    'sources': [
      '../Telegram/Telegram Desktop.entitlements',
    ],
    'defines': [
      'TDESKTOP_DISABLE_AUTOUPDATE',
      'OS_MAC_STORE',
    ],
    'postbuilds': [{
      'postbuild_name': 'Clear Frameworks path',
      'action': [
        'rm', '-rf', '${BUILT_PRODUCTS_DIR}/Telegram Desktop.app/Contents/Frameworks'
      ],
    }, {
      'postbuild_name': 'Force Frameworks path',
      'action': [
        'mkdir', '-p', '${BUILT_PRODUCTS_DIR}/Telegram Desktop.app/Contents/Frameworks/'
      ],
    }, {
      'postbuild_name': 'Copy Breakpad.framework to Frameworks',
      'action': [
        'cp', '-a',
        '<(libs_loc)/breakpad/src/client/mac/build/Release/Breakpad.framework',
        '${BUILT_PRODUCTS_DIR}/Telegram Desktop.app/Contents/Frameworks/Breakpad.framework',
      ],
    }]
  }]],
}
