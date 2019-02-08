/*
  Copyright (c) 2016, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "config.hpp"
#include "dirinfo.hpp"
#include "errno.hpp"
#include "fileinfo.hpp"
#include "fs_base_close.hpp"
#include "fs_base_ioctl.hpp"
#include "fs_base_open.hpp"
#include "fs_path.hpp"
#include "rwlock.hpp"
#include "ugid.hpp"

#include <fuse.h>

#include <string>
#include <vector>

#include <fcntl.h>

using std::string;
using std::vector;

#ifndef FS_IOC_GETFLAGS
# define FS_IOC_GETFLAGS _IOR('f',1,long)
#endif

#ifndef FS_IOC_SETFLAGS
# define FS_IOC_SETFLAGS _IOW('f',2,long)
#endif

namespace l
{
  static
  int
  ioctl(const int            fd_,
        const unsigned long  cmd_,
        void                *data_)
  {
    int rv;

    switch(cmd_)
      {
      case FS_IOC_GETFLAGS:
      case FS_IOC_SETFLAGS:
        {
          int attr;

          attr = *(long*)data_;
          rv = fs::ioctl(fd_,cmd_,(void*)&attr);
          if(rv != -1)
            *(long*)data_ = attr;
        }
        break;
      default:
        switch(_IOC_DIR(cmd_))
          {
          case _IOC_NONE:
            rv = fs::ioctl(fd_,cmd_);
            break;
          default:
            rv = fs::ioctl(fd_,cmd_,data_);
            break;
          }
        break;
      }

    return ((rv == -1) ? -errno : rv);
  }

  static
  int
  ioctl_file(fuse_file_info      *ffi_,
             const unsigned long  cmd_,
             void                *data_)
  {
    FileInfo *fi = reinterpret_cast<FileInfo*>(ffi_->fh);

    return l::ioctl(fi->fd,cmd_,data_);
  }


#ifndef O_NOATIME
#define O_NOATIME 0
#endif

  static
  int
  ioctl_dir_base(Policy::Func::Search  searchFunc_,
                 const Branches       &branches_,
                 const uint64_t        minfreespace_,
                 const char           *fusepath_,
                 const unsigned long   cmd_,
                 void                 *data_)
  {
    int fd;
    int rv;
    string fullpath;
    vector<const string*> basepaths;

    rv = searchFunc_(branches_,fusepath_,minfreespace_,basepaths);
    if(rv == -1)
      return -errno;

    fullpath = fs::path::make(basepaths[0],fusepath_);

    const int flags = O_RDONLY | O_NOATIME | O_NONBLOCK;
    fd = fs::open(fullpath,flags);
    if(fd == -1)
      return -errno;

    rv = l::ioctl(fd,cmd_,data_);

    fs::close(fd);

    return rv;
  }

  static
  int
  ioctl_dir(fuse_file_info      *ffi_,
            const unsigned long  cmd_,
            void                *data_)
  {
    DirInfo                 *di     = reinterpret_cast<DirInfo*>(ffi_->fh);
    const fuse_context      *fc     = fuse_get_context();
    const Config            &config = Config::get(fc);
    const ugid::Set          ugid(fc->uid,fc->gid);
    const rwlock::ReadGuard  readlock(&config.branches_lock);

    return l::ioctl_dir_base(config.open,
                             config.branches,
                             config.minfreespace,
                             di->fusepath.c_str(),
                             cmd_,
                             data_);
  }
}

namespace FUSE
{
  int
  ioctl(const char     *fusepath_,
        int             cmd_,
        void           *arg_,
        fuse_file_info *ffi_,
        unsigned int    flags_,
        void           *data_)
  {
    if(flags_ & FUSE_IOCTL_DIR)
      return l::ioctl_dir(ffi_,cmd_,data_);


    return l::ioctl_file(ffi_,cmd_,data_);
  }
}
