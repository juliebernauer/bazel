// Copyright 2016 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.google.devtools.build.lib.rules.repository;

import static com.google.common.truth.Truth.assertThat;

import com.google.devtools.build.lib.bazel.repository.CompressedTarFunction;
import com.google.devtools.build.lib.bazel.repository.DecompressorDescriptor;
import com.google.devtools.build.lib.bazel.repository.TarGzFunction;
import com.google.devtools.build.lib.testutil.BlazeTestUtils;
import com.google.devtools.build.lib.testutil.TestConstants;
import com.google.devtools.build.lib.testutil.TestUtils;
import com.google.devtools.build.lib.util.OS;
import com.google.devtools.build.lib.vfs.FileSystem;
import com.google.devtools.build.lib.vfs.JavaIoFileSystem;
import com.google.devtools.build.lib.vfs.Path;
import com.google.devtools.build.lib.vfs.UnixFileSystem;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.util.zip.GZIPInputStream;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.JUnit4;

/** Tests decompressing archives. */
@RunWith(JUnit4.class)
public class CompressedTarFunctionTest {

  /* Regular file */
  private static final String REGULAR_FILE_NAME = "regularFile";

  /* Hard link file, created by ln <REGULAR_FILE_NAME> <HARD_LINK_FILE_NAME> */
  private static final String HARD_LINK_FILE_NAME = "hardLinkFile";

  /* Symbolic(Soft) link file, created by ln -s <REGULAR_FILE_NAME> <SYMBOLIC_LINK_FILE_NAME> */
  private static final String SYMBOLIC_LINK_FILE_NAME = "symbolicLinkFile";

  private static final String PATH_TO_TEST_ARCHIVE =
      "/com/google/devtools/build/lib/rules/repository/";

  /* Tarball, created by
   * tar -czf <ARCHIVE_NAME> <REGULAR_FILE_NAME> <HARD_LINK_FILE_NAME> <SYMBOLIC_LINK_FILE_NAME>
   */
  private static final String ARCHIVE_NAME = "test_decompress_archive.tar.gz";

  private FileSystem testFS;
  private Path workingDir;
  private Path tarballPath;
  private Path outDir;
  private DecompressorDescriptor.Builder descriptorBuilder;

  @Before
  public void setUpFs() throws Exception {

    testFS = OS.getCurrent() == OS.WINDOWS ? new JavaIoFileSystem() : new UnixFileSystem();

    tarballPath =
        testFS
            .getPath(BlazeTestUtils.runfilesDir())
            .getRelative(TestConstants.JAVATESTS_ROOT + PATH_TO_TEST_ARCHIVE + ARCHIVE_NAME);

    workingDir = testFS.getPath(new File(TestUtils.tmpDir()).getCanonicalPath());
    outDir = workingDir.getRelative("out");

    descriptorBuilder =
        DecompressorDescriptor.builder()
            .setDecompressor(TarGzFunction.INSTANCE)
            .setRepositoryPath(outDir)
            .setArchivePath(tarballPath);
  }

  /**
   * Test decompressing a tar.gz file with hard link file and symbolic link file inside
   *
   * @throws Exception
   */
  @Test
  public void testDecompress() throws Exception {

    Path outputDir =
        new CompressedTarFunction() {
          @Override
          protected InputStream getDecompressorStream(DecompressorDescriptor descriptor)
              throws IOException {
            return new GZIPInputStream(new FileInputStream(descriptor.archivePath().getPathFile()));
          }
        }.decompress(descriptorBuilder.build());

    assertThat(outputDir.exists()).isTrue();
    assertThat(outputDir.getRelative(REGULAR_FILE_NAME).exists()).isTrue();
    assertThat(outputDir.getRelative(REGULAR_FILE_NAME).getFileSize()).isNotEqualTo(0);
    assertThat(outputDir.getRelative(REGULAR_FILE_NAME).isSymbolicLink()).isFalse();
    assertThat(outputDir.getRelative(HARD_LINK_FILE_NAME).exists()).isTrue();
    assertThat(outputDir.getRelative(HARD_LINK_FILE_NAME).getFileSize()).isNotEqualTo(0);
    assertThat(outputDir.getRelative(HARD_LINK_FILE_NAME).isSymbolicLink()).isFalse();
    assertThat(outputDir.getRelative(SYMBOLIC_LINK_FILE_NAME).exists()).isTrue();
    assertThat(outputDir.getRelative(SYMBOLIC_LINK_FILE_NAME).getFileSize()).isNotEqualTo(0);
    assertThat(outputDir.getRelative(SYMBOLIC_LINK_FILE_NAME).isSymbolicLink()).isTrue();
    assertThat(
            Files.isSameFile(
                java.nio.file.Paths.get(outputDir.getRelative(REGULAR_FILE_NAME).toString()),
                java.nio.file.Paths.get(outputDir.getRelative(HARD_LINK_FILE_NAME).toString())))
        .isTrue();
    assertThat(
            Files.isSameFile(
                java.nio.file.Paths.get(outputDir.getRelative(REGULAR_FILE_NAME).toString()),
                java.nio.file.Paths.get(outputDir.getRelative(SYMBOLIC_LINK_FILE_NAME).toString())))
        .isTrue();
  }
}
