#!/bin/bash

qbs build \
    --file qgit.qbs \
    --build-directory ./build \
    --command-echo-mode command-line \
    --jobs 4 \
    --no-install \
    profile:qt486 release
