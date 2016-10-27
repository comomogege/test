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
  'conditions': [
    [ 'build_win', {
      'defines': [
        'WIN32',
        '_WINDOWS',
        '_UNICODE',
        'UNICODE',
        'HAVE_STDINT_H',
        'ZLIB_WINAPI',
        '_SCL_SECURE_NO_WARNINGS',
        '_USING_V110_SDK71_',
      ],
      'msvs_cygwin_shell': 0,
      'msvs_settings': {
        'VCCLCompilerTool': {
          'ProgramDataBaseFileName': '$(OutDir)\\$(ProjectName).pdb',
          'DebugInformationFormat': '3',          # Program Database (/Zi)
          'AdditionalOptions': [
            '/MP',   # Enable multi process build.
            '/EHsc', # Catch C++ exceptions only, extern C functions never throw a C++ exception.
          ],
          'TreatWChar_tAsBuiltInType': 'false',
        },
        'VCLinkerTool': {
          'MinimumRequiredVersion': '5.01',
          'ImageHasSafeExceptionHandlers': 'false',   # Disable /SAFESEH
        },
      },
      'msvs_external_builder_build_cmd': [
        'ninja.exe',
        '-C',
        '$(OutDir)',
        '-k0',
        '$(ProjectName)',
      ],
      'libraries': [
        'winmm',
        'imm32',
        'ws2_32',
        'kernel32',
        'user32',
        'gdi32',
        'winspool',
        'comdlg32',
        'advapi32',
        'shell32',
        'ole32',
        'oleaut32',
        'uuid',
        'odbc32',
        'odbccp32',
        'Shlwapi',
        'Iphlpapi',
        'Gdiplus',
        'Strmiids',
      ],

      'configurations': {
        'Debug': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'Optimization': '0',                # Disabled (/Od)
              'RuntimeLibrary': '1',              # Multi-threaded Debug (/MTd)
            },
            'VCLinkerTool': {
              'GenerateDebugInformation': 'true', # true (/DEBUG)
              'IgnoreDefaultLibraryNames': 'LIBCMT',
              'LinkIncremental': '2',             # Yes (/INCREMENTAL)
            },
          },
        },
        'Release': {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'Optimization': '2',                 # Maximize Speed (/O2)
              'InlineFunctionExpansion': '2',      # Any suitable (/Ob2)
              'EnableIntrinsicFunctions': 'true',  # Yes (/Oi)
              'FavorSizeOrSpeed': '1',             # Favor fast code (/Ot)
              'RuntimeLibrary': '0',               # Multi-threaded (/MT)
              'EnableEnhancedInstructionSet': '2', # Streaming SIMD Extensions 2 (/arch:SSE2)
              'WholeProgramOptimization': 'true',  # /GL
            },
            'VCLinkerTool': {
              'GenerateDebugInformation': 'true',  # /DEBUG
              'OptimizeReferences': '2',
              'LinkTimeCodeGeneration': '1',       # /LTCG
            },
          },
        },
      },
      'conditions': [
        [ '"<(official_build_target)" != "" and "<(official_build_target)" != "win"', {
          'sources': [ '__Wrong_Official_Build_Target__' ],
        }],
      ],
    }],
  ],
}
