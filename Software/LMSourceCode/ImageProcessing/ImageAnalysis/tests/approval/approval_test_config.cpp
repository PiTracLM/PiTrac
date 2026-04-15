/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "approval_test_config.hpp"
#include <cstdlib>
#include <stdexcept>

namespace golf_sim::image_analysis::testing {

    const ApprovalTestConfig& ApprovalTestConfig::Instance() {
        static ApprovalTestConfig instance;
        return instance;
    }

    ApprovalTestConfig::ApprovalTestConfig() {
        // Constructor intentionally minimal - all initialization in member initializer list
    }

    bool ApprovalTestConfig::IsRunningInCI() const {
        return std::getenv("CI") != nullptr || std::getenv("GITHUB_ACTIONS") != nullptr ||
               std::getenv("TF_BUILD") != nullptr;
    }

    void ApprovalTestConfig::EnsureDirectoriesExist() const {
        try {
            std::filesystem::create_directories(approval_artifacts_dir_);
        } catch (const std::filesystem::filesystem_error& e) {
            throw std::runtime_error("Failed to create approval artifacts directory: " +
                                     approval_artifacts_dir_ + " - " + e.what());
        }
    }

}  // namespace golf_sim::image_analysis::testing
