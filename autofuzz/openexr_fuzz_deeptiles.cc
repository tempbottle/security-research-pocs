#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfDeepTiledInputFile.h>
#include <ImfDeepTiledInputPart.h>
#include <ImfTiledInputPart.h>

namespace IMF = OPENEXR_IMF_NAMESPACE;
using namespace IMF;
using IMATH_NAMESPACE::Box2i;

namespace {

template <typename T>
static void readFile(T *part) {
  const Header &fileHeader = part->header();

  Array2D<unsigned int> localSampleCount;

  Box2i dataWindow = fileHeader.dataWindow();

  int height = dataWindow.size().y + 1;
  int width = dataWindow.size().x + 1;

  localSampleCount.resizeErase(height, width);

  int channelCount = 0;
  for (ChannelList::ConstIterator i = fileHeader.channels().begin();
       i != fileHeader.channels().end(); ++i, channelCount++) {
  }

  Array<Array2D<void *> > data(channelCount);

  for (int i = 0; i < channelCount; i++) {
    data[i].resizeErase(height, width);
  }

  DeepFrameBuffer frameBuffer;

  int memOffset = dataWindow.min.x + dataWindow.min.y * width;
  frameBuffer.insertSampleCountSlice(
      Slice(IMF::UINT, (char *)(&localSampleCount[0][0] - memOffset),
            sizeof(unsigned int) * 1, sizeof(unsigned int) * width));

  for (int i = 0; i < channelCount; i++) {
    std::stringstream ss;
    ss << i;
    std::string str = ss.str();

    int sampleSize = sizeof(float);

    int pointerSize = sizeof(char *);

    frameBuffer.insert(
        str, DeepSlice(IMF::FLOAT, (char *)(&data[i][0][0] - memOffset),
                       pointerSize * 1, pointerSize * width, sampleSize));
  }

  part->setFrameBuffer(frameBuffer);
  for (int ly = 0; ly < part->numYLevels(); ly++) {
    for (int lx = 0; lx < part->numXLevels(); lx++) {
      Box2i dataWindowL = part->dataWindowForLevel(lx, ly);

      part->readPixelSampleCounts(0, part->numXTiles(lx) - 1, 0,
                                  part->numYTiles(ly) - 1, lx, ly);

      for (int i = 0; i < part->numYTiles(ly); i++) {
        for (int j = 0; j < part->numXTiles(lx); j++) {
          Box2i box = part->dataWindowForTile(j, i, lx, ly);
          for (int y = box.min.y; y <= box.max.y; y++)
            for (int x = box.min.x; x <= box.max.x; x++) {
              int dwy = y - dataWindowL.min.y;
              int dwx = x - dataWindowL.min.x;

              for (int k = 0; k < channelCount; k++) {
                data[k][dwy][dwx] = new float[localSampleCount[dwy][dwx]];
              }
            }
        }
      }

      try {
        part->readTiles(0, part->numXTiles(lx) - 1, 0, part->numYTiles(ly) - 1,
                        lx, ly);
      } catch (...) {
      }

      for (int i = 0; i < part->levelHeight(ly); i++) {
        for (int j = 0; j < part->levelWidth(lx); j++) {
          for (int k = 0; k < channelCount; k++) {
            delete[](float *) data[k][i][j];
          }
        }
      }
    }
  }
}

static void readFileSingle(const char filename[]) {
  DeepTiledInputFile *file;
  try {
    file = new DeepTiledInputFile(filename, 8);
  } catch (...) {
    return;
  }

  try {
    readFile(file);
  } catch (std::exception &e) {
  }

  delete file;
}

static void readFileMulti(const char filename[]) {
  MultiPartInputFile *file;

  try {
    file = new MultiPartInputFile(filename, 8);
  } catch (...) {
    return;
  }

  for (int p = 0; p < file->parts(); p++) {
    DeepTiledInputPart *part;
    try {
      part = new DeepTiledInputPart(*file, p);
    } catch (...) {
      continue;
    }

    try {
      readFile(part);
    } catch (...) {
    }

    delete part;
  }

  delete file;
}

}  // namespace

// from cl/164883104
static char *buf_to_file(const char *buf, size_t size) {
  char *name = strdup("/dev/shm/fuzz-XXXXXX");
  int fd = mkstemp(name);
  if (fd < 0) {
    perror("open");
    exit(1);
  }
  size_t pos = 0;
  while (pos < size) {
    int nbytes = write(fd, &buf[pos], size - pos);
    if (nbytes <= 0) {
      perror("write");
      exit(1);
    }
    pos += nbytes;
  }
  if (close(fd) != 0) {
    perror("close");
    exit(1);
  }
  return name;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char *file = buf_to_file((const char *)data, size);
  Header::setMaxImageSize(10000, 10000);
  readFileSingle(file);
  readFileMulti(file);
  unlink(file);
  free(file);
  return 0;
}
