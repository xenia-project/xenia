# Copyright 2013 Ben Vanik. All Rights Reserved.
{
  'targets': [
    {
      'target_name': 'wslay',
      'type': '<(library)',

      'direct_dependent_settings': {
        'include_dirs': [
          'wslay/lib/includes/',
        ],

        'defines': [
          'WSLAY_VERSION=1',
        ],

        # libraries: ws2_32 on windows
      },

      'defines': [
        'WSLAY_VERSION="1"',
      ],

      'conditions': [
        ['OS != "win"', {
          'defines': [
            'HAVE_ARPA_INET_H=1',
            'HAVE_NETINET_IN_H=1',
          ],
        }],
        ['OS == "win"', {
          'defines': [
            'HAVE_WINSOCK2_H=1',
            'ssize_t=unsigned long long',
          ],
        }],
      ],

      'include_dirs': [
        'wslay/lib/',
        'wslay/lib/includes/',
      ],

      'sources': [
        'wslay/lib/includes/wslay/wslay.h',
        'wslay/lib/wslay_event.c',
        'wslay/lib/wslay_event.h',
        'wslay/lib/wslay_frame.c',
        'wslay/lib/wslay_frame.h',
        'wslay/lib/wslay_net.c',
        'wslay/lib/wslay_net.h',
        'wslay/lib/wslay_queue.c',
        'wslay/lib/wslay_queue.h',
        'wslay/lib/wslay_stack.c',
        'wslay/lib/wslay_stack.h',
      ],
    }
  ]
}
