// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024-2025, Advanced Micro Devices, Inc. All rights reserved.

#include "io.h"
#include "hwctx.h"
#include "exec_buf.h"
#include "io_config.h"

#include <string>
#include <regex>

using namespace xrt_core;

namespace {

const char *io_test_bo_type_names[] = {
  "IO_TEST_BO_CMD",
  "IO_TEST_BO_INSTRUCTION",
  "IO_TEST_BO_INPUT",
  "IO_TEST_BO_PARAMETERS",
  "IO_TEST_BO_OUTPUT",
  "IO_TEST_BO_INTERMEDIATE",
  "IO_TEST_BO_MC_CODE",
  "IO_TEST_BO_BAD_INSTRUCTION",
};

void
alloc_bo(io_test_bo& ibo, device* dev, io_test_bo_type t)
{
  auto sz = ibo.size;

  if (sz == 0) {
    ibo.tbo = nullptr;
    return;
  }

  switch(t) {
  case IO_TEST_BO_CMD:
    ibo.tbo = std::make_shared<bo>(dev, sz, XCL_BO_FLAGS_EXECBUF);
    break;
  case IO_TEST_BO_INSTRUCTION:
    ibo.tbo = std::make_shared<bo>(dev, sz, XCL_BO_FLAGS_CACHEABLE);
    break;
  default:
    ibo.tbo = std::make_shared<bo>(dev, sz);
    break;
  }
}

void
init_bo(io_test_bo& ibo, const std::string& bin)
{
  read_data_from_bin(bin, ibo.init_offset, ibo.tbo->size() - ibo.init_offset, ibo.tbo->map());
}

size_t
get_bin_size(const std::string& filename)
{
  std::ifstream ifs(filename, std::ifstream::ate | std::ifstream::binary);
  if (!ifs.is_open())
    throw std::runtime_error("Failure opening file " + filename + "!!");
  return ifs.tellg();
}

}

io_test_bo_set_base::
io_test_bo_set_base(device* dev, const std::string& xclbin_name) :
  m_bo_array{}
  , m_xclbin_name(xclbin_name)
  , m_local_data_path(get_xclbin_data(dev, xclbin_name.c_str()))
  , m_dev(dev)
{
}

io_test_bo_set::
io_test_bo_set(device* dev, const std::string& xclbin_name) :
  io_test_bo_set_base(dev, xclbin_name)
{
  std::string file;
  auto tp = parse_config_file(m_local_data_path + config_file);

  for (int i = 0; i < IO_TEST_BO_MAX_TYPES; i++) {
    auto& ibo = m_bo_array[i];
    auto type = static_cast<io_test_bo_type>(i);

    switch(type) {
    case IO_TEST_BO_CMD:
      ibo.size = 0x1000;
      alloc_bo(ibo, m_dev, type);
      break;
    case IO_TEST_BO_INSTRUCTION:
      file = m_local_data_path + instr_file;
      ibo.size = get_instr_size(file) * sizeof(int32_t);
      if (ibo.size == 0)
        throw std::runtime_error("instruction size cannot be 0");
      alloc_bo(ibo, m_dev, type);
      read_instructions_from_txt(file, ibo.tbo->map());
      break;
    case IO_TEST_BO_INPUT:
      ibo.size = IFM_SIZE(tp);
      ibo.init_offset = IFM_DIRTY_BYTES(tp);
      alloc_bo(ibo, m_dev, type);
      init_bo(ibo, m_local_data_path + ifm_file);
      break;
    case IO_TEST_BO_PARAMETERS:
      ibo.size = PARAM_SIZE(tp);
      alloc_bo(ibo, m_dev, type);
      init_bo(ibo, m_local_data_path + param_file);
      break;
    case IO_TEST_BO_OUTPUT:
      ibo.size = OFM_SIZE(tp);
      alloc_bo(ibo, m_dev, type);
      break;
    case IO_TEST_BO_INTERMEDIATE:
      ibo.size = INTER_SIZE(tp);
      alloc_bo(ibo, m_dev, type);
      break;
    case IO_TEST_BO_MC_CODE:
      // Do not support patching MC_CODE. */
      if (MC_CODE_SIZE(tp))
        throw std::runtime_error("MC_CODE_SIZE is non zero!!!");
      ibo.size = DUMMY_MC_CODE_BUFFER_SIZE;
      alloc_bo(ibo, m_dev, type);
      break;
    default:
      throw std::runtime_error("unknown BO type");
      break;
    }
  }
}

io_test_bo_set::
io_test_bo_set(device* dev) : io_test_bo_set(dev, get_xclbin_name(dev))
{
}

elf_io_test_bo_set::
elf_io_test_bo_set(device* dev, const std::string& xclbin_name) :
  io_test_bo_set_base(dev, xclbin_name)
  , m_elf_path(m_local_data_path + "/no-ctrl-packet.elf")
{
  std::string file;

  for (int i = 0; i < IO_TEST_BO_MAX_TYPES; i++) {
    auto& ibo = m_bo_array[i];
    auto type = static_cast<io_test_bo_type>(i);

    switch(type) {
    case IO_TEST_BO_CMD:
      ibo.size = 0x1000;
      alloc_bo(ibo, m_dev, type);
      break;
    case IO_TEST_BO_INSTRUCTION:
      ibo.size = exec_buf::get_ctrl_code_size(m_elf_path);
      if (ibo.size == 0)
        throw std::runtime_error("instruction size cannot be 0");
      alloc_bo(ibo, m_dev, type);
      break;
    case IO_TEST_BO_INPUT:
      file = m_local_data_path + "/ifm.bin";
      ibo.size = get_bin_size(file);
      alloc_bo(ibo, m_dev, type);
      init_bo(ibo, file);
      break;
    case IO_TEST_BO_PARAMETERS:
      file = m_local_data_path + "/wts.bin";
      ibo.size = get_bin_size(file);
      alloc_bo(ibo, m_dev, type);
      init_bo(ibo, file);
      break;
    case IO_TEST_BO_OUTPUT:
      file = m_local_data_path + "/ofm.bin";
      ibo.size = get_bin_size(file);
      alloc_bo(ibo, m_dev, type);
      break;
    case IO_TEST_BO_INTERMEDIATE:
    case IO_TEST_BO_MC_CODE:
      // No need for intermediate/mc_code BO
      break;
    default:
      throw std::runtime_error("unknown BO type");
      break;
    }
  }
}

void
io_test_bo_set_base::
sync_before_run()
{
  for (int i = 0; i < IO_TEST_BO_MAX_TYPES; i++) {
    io_test_bo *ibo = &m_bo_array[i];

    if (ibo->tbo == nullptr)
      continue;

    switch(i) {
    case IO_TEST_BO_INPUT:
    case IO_TEST_BO_INSTRUCTION:
    case IO_TEST_BO_PARAMETERS:
    case IO_TEST_BO_MC_CODE:
      ibo->tbo->get()->sync(buffer_handle::direction::host2device, ibo->tbo->size(), 0);
      break;
    default:
      break;
    }
  }
}

void
io_test_bo_set_base::
sync_after_run()
{
  for (int i = 0; i < IO_TEST_BO_MAX_TYPES; i++) {
    io_test_bo *ibo = &m_bo_array[i];

    if (ibo->tbo == nullptr)
      continue;

    switch(i) {
    case IO_TEST_BO_OUTPUT:
    case IO_TEST_BO_INTERMEDIATE:
      ibo->tbo->get()->sync(buffer_handle::direction::device2host, ibo->tbo->size(), 0);
      break;
    default:
      break;
    }
  }
}

void
io_test_bo_set::
init_cmd(xrt_core::cuidx_type idx, bool dump)
{
  exec_buf ebuf(*m_bo_array[IO_TEST_BO_CMD].tbo.get(), ERT_START_CU);

  ebuf.set_cu_idx(idx);
  ebuf.add_arg_64(1);
  ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_INPUT].tbo.get());
  ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_PARAMETERS].tbo.get());
  ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_OUTPUT].tbo.get());
  ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_INTERMEDIATE].tbo.get());
  ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_INSTRUCTION].tbo.get());
  ebuf.add_arg_32(m_bo_array[IO_TEST_BO_INSTRUCTION].tbo->size() / sizeof(int32_t));
  ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_MC_CODE].tbo.get());
  if (dump)
    ebuf.dump();
}

void
elf_io_test_bo_set::
init_cmd(xrt_core::cuidx_type idx, bool dump)
{
  auto dev_id = device_query<query::pcie_device>(m_dev);

  exec_buf ebuf(*m_bo_array[IO_TEST_BO_CMD].tbo.get(), ERT_START_NPU);

  ebuf.set_cu_idx(idx);
  if (dev_id == npu1_device_id) {
    ebuf.add_ctrl_bo(*m_bo_array[IO_TEST_BO_INSTRUCTION].tbo.get());
    ebuf.add_arg_32(3);
    ebuf.add_arg_64(0);
    ebuf.add_arg_64(0);
    ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_PARAMETERS].tbo.get());
    ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_INPUT].tbo.get());
    ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_OUTPUT].tbo.get());
    ebuf.add_arg_64(0);
    ebuf.add_arg_64(0);
    ebuf.patch_ctrl_code(*m_bo_array[IO_TEST_BO_INSTRUCTION].tbo.get(), m_elf_path);
  } else if (dev_id == npu4_device_id) {
    ebuf.add_ctrl_bo(*m_bo_array[IO_TEST_BO_INSTRUCTION].tbo.get());
    ebuf.add_arg_32(3);
    ebuf.add_arg_64(0);
    ebuf.add_arg_64(0);
    ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_INPUT].tbo.get());
    ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_PARAMETERS].tbo.get());
    ebuf.add_arg_bo(*m_bo_array[IO_TEST_BO_OUTPUT].tbo.get());
    ebuf.add_arg_64(0);
    ebuf.add_arg_64(0);
    ebuf.patch_ctrl_code(*m_bo_array[IO_TEST_BO_INSTRUCTION].tbo.get(), m_elf_path);
  } else {
    throw std::runtime_error("Device ID not supported: " + std::to_string(dev_id));
  }
  if (dump)
    ebuf.dump();
}

// For debug only
void
io_test_bo_set_base::
dump_content()
{
  for (int i = 0; i < IO_TEST_BO_MAX_TYPES; i++) {
    auto ibo = m_bo_array[i].tbo.get();

    if (ibo == nullptr)
      continue;

    auto ibo_p = reinterpret_cast<int8_t *>(ibo->map());
    std::string p("/tmp/");
    p += io_test_bo_type_names[i] + std::to_string(getpid());
    dump_buf_to_file(ibo_p, ibo->size(), p);
    std::cout << "Dumping BO to: " << p << std::endl;
  }
}

void
io_test_bo_set::
verify_result()
{
  auto ofm_bo = m_bo_array[IO_TEST_BO_OUTPUT].tbo.get();
  auto ofm_p = reinterpret_cast<int8_t *>(ofm_bo->map());

  if (verify_output(ofm_p, m_local_data_path))
    throw std::runtime_error("Test failed!!!");
}

void
elf_io_test_bo_set::
verify_result()
{
  auto bo_ofm = m_bo_array[IO_TEST_BO_OUTPUT].tbo;
  auto ofm_p = reinterpret_cast<char*>(bo_ofm->map());
  auto sz = bo_ofm->size();

  std::vector<char> buf_ofm_golden(sz);
  auto ofm_golden_p = reinterpret_cast<char*>(buf_ofm_golden.data());
  read_data_from_bin(m_local_data_path + "/ofm.bin", 0, sz, reinterpret_cast<int*>(ofm_golden_p));

  size_t count = 0;
  for (size_t i = 0; i < sz; i++) {
    if (ofm_p[i] != ofm_golden_p[i])
      count++;
  }
  if (count)
    throw std::runtime_error(std::to_string(count) + " bytes result mismatch!!!");
}

const char *
io_test_bo_set_base::
bo_type2name(int type)
{
  return io_test_bo_type_names[type];
}

void
io_test_bo_set_base::
run(const std::vector<xrt_core::fence_handle*>& wait_fences,
  const std::vector<xrt_core::fence_handle*>& signal_fences, bool no_check_result)
{
  hw_ctx hwctx{m_dev, m_xclbin_name.c_str()};
  auto hwq = hwctx.get()->get_hw_queue();
  auto kernel = get_kernel_name(m_dev, m_xclbin_name.c_str());
  if (kernel.empty())
    throw std::runtime_error("No kernel found");
  auto cu_idx = hwctx.get()->open_cu_context(kernel);
  std::cout << "Found kernel: " << kernel << " with cu index " << cu_idx.index << std::endl;

  init_cmd(cu_idx, false);
  sync_before_run();

  auto cbo = m_bo_array[IO_TEST_BO_CMD].tbo.get();
  auto chdl = cbo->get();
  for (const auto& fence : wait_fences)
    hwq->submit_wait(fence);
  hwq->submit_command(chdl);
  for (const auto& fence : signal_fences)
    hwq->submit_signal(fence);
  hwq->wait_command(chdl, 5000);
  auto cpkt = reinterpret_cast<ert_start_kernel_cmd *>(cbo->map());
  if (cpkt->state != ERT_CMD_STATE_COMPLETED)
    throw std::runtime_error(std::string("Command failed, state=") + std::to_string(cpkt->state));

  sync_after_run();
  if (!no_check_result)
    verify_result();
}

void
io_test_bo_set_base::
run()
{
  const std::vector<xrt_core::fence_handle*> sfences{};
  const std::vector<xrt_core::fence_handle*> wfences{};
  run(wfences, sfences, false);
}

void
io_test_bo_set_base::
run_no_check_result()
{
  const std::vector<xrt_core::fence_handle*> sfences{};
  const std::vector<xrt_core::fence_handle*> wfences{};
  run(wfences, sfences, true);
}

std::array<io_test_bo, IO_TEST_BO_MAX_TYPES>&
io_test_bo_set_base::
get_bos()
{
  return m_bo_array;
}
