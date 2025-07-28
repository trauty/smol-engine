pipeline {
  agent {
    label 'smol-engine'
  }

  environment {
    BUILD_TYPE = 'Release'
    LINUX_CLANG_PRESET = 'linux-clang-release-x64'
    WINDOWS_CLANG_CROSS_PRESET = 'windows-clang-release-cross-x64'
    LLVM_MINGW_DIR = '/opt/llvm-mingw'
  }

  options {
    timestamps()
  }

  stages {
    stage('Build (Linux Clang)') {
      steps {
        sh '''
          cmake --preset $LINUX_CLANG_PRESET
          cmake --build --preset $LINUX_CLANG_PRESET --target install
        '''
      }
    }

    stage('Package (Linux Clang)') {
      steps {
        sh '''
          mv build/$LINUX_CLANG_PRESET/install ./$LINUX_CLANG_PRESET
        '''
        archiveArtifacts artifacts: "$LINUX_CLANG_PRESET/**", fingerprint: true
      }
    }
  }
}
