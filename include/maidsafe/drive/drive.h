/*  Copyright 2011 MaidSafe.net limited

    This MaidSafe Software is licensed to you under (1) the MaidSafe.net Commercial License,
    version 1.0 or later, or (2) The General Public License (GPL), version 3, depending on which
    licence you accepted on initial access to the Software (the "Licences").

    By contributing code to the MaidSafe Software, or to this project generally, you agree to be
    bound by the terms of the MaidSafe Contributor Agreement, version 1.0, found in the root
    directory of this project at LICENSE, COPYING and CONTRIBUTOR respectively and also
    available at: http://www.maidsafe.net/licenses

    Unless required by applicable law or agreed to in writing, the MaidSafe Software distributed
    under the GPL Licence is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS
    OF ANY KIND, either express or implied.

    See the Licences for the specific language governing permissions and limitations relating to
    use of the MaidSafe Software.                                                                 */

#ifndef MAIDSAFE_DRIVE_DRIVE_H_
#define MAIDSAFE_DRIVE_DRIVE_H_

#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "boost/filesystem/path.hpp"

#include "maidsafe/common/rsa.h"
#include "maidsafe/drive/config.h"
#include "maidsafe/drive/meta_data.h"
#include "maidsafe/drive/directory_handler.h"
#include "maidsafe/drive/utils.h"


namespace maidsafe {
namespace drive {

template <typename Storage>
class Drive {
 public:
  typedef std::shared_ptr<Storage> StoragePtr;
  typedef encrypt::SelfEncryptor<Storage> SelfEncryptor;
  typedef detail::FileContext<Storage> FileContext;
  typedef FileContext* FileContextPtr;
  typedef detail::DirectoryHandler<Storage> DirectoryHandler;
  typedef detail::Directory Directory;
  typedef detail::OpType OpType;

  Drive(StoragePtr storage, const Identity& unique_user_id, const Identity& root_parent_id,
        const boost::filesystem::path& mount_dir);

  virtual ~Drive() {}

#ifdef MAIDSAFE_APPLE
  boost::filesystem::path GetMountDir() const { return kMountDir_; }
#endif

  Identity root_parent_id() const;

  // ********************* File / Folder Transfers ************************************************

  // Retrieve the serialised DataMap of the file at 'relative_path' (e.g. to send to another client)
  std::string GetDataMap(const boost::filesystem::path& relative_path);
  std::string GetHiddenDataMap(const boost::filesystem::path& relative_path);
  // Insert a file at 'relative_path' derived from the serialised DataMap (e.g. if
  // receiving from another client).
  void InsertDataMap(const boost::filesystem::path& relative_path,
                     const std::string& serialised_data_map);

  // Adds a directory or file represented by 'meta_data' and 'relative_path' to the appropriate
  // parent directory listing. If the element is a directory, a new directory listing is created
  // and stored. The parent directory's ID is returned in 'parent_id' and its parent directory's ID
  // is returned in 'grandparent_id'.
  void Add(const boost::filesystem::path& relative_path, FileContext& file_context);
  // Deletes the file at 'relative_path' from the appropriate parent directory listing as well as
  // the listing associated with that path if it represents a directory.
  void Delete(const boost::filesystem::path& relative_path);
  bool CanDelete(const boost::filesystem::path& relative_path);
  // Rename/move the file associated with 'meta_data' located at 'old_relative_path' to that at
  // 'new_relative_path'.
  void Rename(const boost::filesystem::path& old_relative_path,
              const boost::filesystem::path& new_relative_path, MetaData& meta_data);

   protected:
  // Recovers Directory for 'relative_path'.
  Directory GetDirectory(const boost::filesystem::path& relative_path);
  // Returns FileContext associated with 'relative_path'.
  FileContext GetFileContext(const boost::filesystem::path& relative_path);
  // Updates parent directory at 'parent_path' with the values contained in the 'file_context'.
  void UpdateParent(FileContextPtr file_context, const boost::filesystem::path& parent_path);
  // Resizes the file.
  bool TruncateFile(FileContextPtr file_context, const uint64_t& size);
  // virtual void NotifyRename(const boost::filesystem::path& from_relative_path,
  //                           const boost::filesystem::path& to_relative_path) const = 0;

  DirectoryHandler directory_handler_;
  StoragePtr storage_;
  const boost::filesystem::path kMountDir_;

 private:
  virtual void SetNewAttributes(FileContextPtr file_context, bool is_directory,
                                bool read_only) = 0;
  std::string ReadDataMap(const boost::filesystem::path& relative_path);

};

#ifdef MAIDSAFE_WIN32
inline boost::filesystem::path GetNextAvailableDrivePath() {
  uint32_t drive_letters(GetLogicalDrives()), mask(0x4);
  std::string path("C:");
  while (drive_letters & mask) {
    mask <<= 1;
    ++path[0];
  }
  if (path[0] > 'Z')
    ThrowError(DriveErrors::no_drive_letter_available);
  return boost::filesystem::path(path);
}
#endif

// ==================== Implementation ============================================================

template <typename Storage>
Drive<Storage>::Drive(StoragePtr storage, const Identity& unique_user_id,
                      const Identity& root_parent_id, const boost::filesystem::path& mount_dir)
    : directory_handler_(storage, unique_user_id, root_parent_id),
      storage_(storage),
      kMountDir_(mount_dir) {}

template <typename Storage>
Identity Drive<Storage>::root_parent_id() const {
  return directory_handler_.root_parent_id();
}


template <typename Storage>
void Drive<Storage>::Add(const boost::filesystem::path& relative_path,
                         FileContext& file_context) {
  directory_handler_.Add(relative_path, *file_context.meta_data,
                         file_context.grandparent_directory_id, file_context.parent_directory_id);
}

template <typename Storage>
void Drive<Storage>::Delete(const boost::filesystem::path& relative_path) {
  directory_handler_.Delete(relative_path);
}

template <typename Storage>
void Drive<Storage>::Rename(const boost::filesystem::path& old_relative_path,
                            const boost::filesystem::path& new_relative_path,
                            MetaData& meta_data) {
  directory_handler_.Rename(old_relative_path, new_relative_path, meta_data);
}

// ********************** File / Folder Transfers *************************************************

template <typename Storage>
std::string Drive<Storage>::GetDataMap(const boost::filesystem::path& relative_path) {
  return ReadDataMap(relative_path);
}

template <typename Storage>
void Drive<Storage>::InsertDataMap(const boost::filesystem::path& relative_path,
                                   const std::string& serialised_data_map) {
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(relative_path.filename(), false);
  encrypt::ParseDataMap(serialised_data_map, *file_context.meta_data->data_map);
  SetNewAttributes(&file_context, false, false);
  Add(relative_path, file_context);
}

// **************************** Miscellaneous *****************************************************

template <typename Storage>
typename Drive<Storage>::Directory Drive<Storage>::GetDirectory(
    const boost::filesystem::path& relative_path) {
  return directory_handler_.Get(relative_path);
}

template <typename Storage>
typename Drive<Storage>::FileContext Drive<Storage>::GetFileContext(
    const boost::filesystem::path& relative_path) {
  FileContext file_context;
  Directory parent(directory_handler_.Get(relative_path.parent_path()));
  parent.listing->GetChild(relative_path.filename(), *file_context.meta_data);
  file_context.grandparent_directory_id = parent.parent_id;
  file_context.parent_directory_id = parent.listing->directory_id();
  return file_context;
}

template <typename Storage>
void Drive<Storage>::UpdateParent(FileContextPtr file_context,
                                  const boost::filesystem::path& parent_path) {
  directory_handler_.UpdateParent(parent_path, *file_context->meta_data);
}

template <typename Storage>
bool Drive<Storage>::TruncateFile(FileContextPtr file_context, const uint64_t& size) {
  if (!file_context->self_encryptor) {
    file_context->self_encryptor.reset(
        new SelfEncryptor(file_context->meta_data->data_map, *storage_));
  }
  bool result = file_context->self_encryptor->Truncate(size);
  if (result) {
    file_context->content_changed = true;
  }
  return result;
}

template <typename Storage>
std::string Drive<Storage>::ReadDataMap(const boost::filesystem::path& relative_path) {
  std::string serialised_data_map;
  if (relative_path.empty())
    ThrowError(CommonErrors::invalid_parameter);

  FileContext file_context(GetFileContext(relative_path));

  if (!file_context.meta_data->data_map)
    ThrowError(CommonErrors::invalid_parameter);

  try {
    encrypt::SerialiseDataMap(*file_context.meta_data->data_map, serialised_data_map);
  }
  catch(const std::exception& exception) {
    boost::throw_exception(exception);
  }
  return serialised_data_map;
}

}  // namespace drive
}  // namespace maidsafe

#endif  // MAIDSAFE_DRIVE_DRIVE_H_
