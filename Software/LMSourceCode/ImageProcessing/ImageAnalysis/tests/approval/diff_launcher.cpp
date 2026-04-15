/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "diff_launcher.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace golf_sim::image_analysis::testing {

    // VSCodeDiffLauncher Implementation
    VSCodeDiffLauncher::VSCodeDiffLauncher(const ApprovalTestConfig& config) : config_(config) {}

    bool VSCodeDiffLauncher::LaunchTextDiff(const DiffInfo& diff_info) const {
        if (!ShouldLaunchDiff()) {
            std::cout << "CI environment detected - skipping interactive text diff for "
                      << diff_info.test_name << std::endl;
            return true;  // Return true since this is expected behavior in CI
        }

        std::cout << "🔍 DIFF TRACE: Launching VS Code text diff for " << diff_info.test_name
                  << "..." << std::endl;
        std::cout << "🔍 DIFF TRACE: Approved: " << diff_info.approved_path << std::endl;
        std::cout << "🔍 DIFF TRACE: Received: " << diff_info.received_path << std::endl;

        std::string command;

        if (diff_info.is_new_baseline) {
            // Create empty baseline for comparison
            std::string empty_baseline = diff_info.approved_path + ".empty";
            if (!CreateEmptyBaseline(empty_baseline, "text")) {
                return false;
            }

            command = "code --diff \"" + empty_baseline + "\" \"" + diff_info.received_path + "\"";
            std::cout << "To approve this baseline, run:" << std::endl;
            std::cout << "  copy \"" << diff_info.received_path << "\" \""
                      << diff_info.approved_path << "\"" << std::endl;
        } else {
            command = "code --diff \"" + diff_info.approved_path + "\" \"" +
                      diff_info.received_path + "\"";
            std::cout << "To approve changes, run:" << std::endl;
            std::cout << "  copy \"" << diff_info.received_path << "\" \""
                      << diff_info.approved_path << "\"" << std::endl;
        }

        std::cout << "🔍 DIFF TRACE: Executing text diff command: " << command << std::endl;
        return ExecuteCommand(command);
    }

    bool VSCodeDiffLauncher::LaunchImageDiff(const DiffInfo& diff_info) const {
        if (!ShouldLaunchDiff()) {
            std::cout << "CI environment detected - skipping interactive image diff for "
                      << diff_info.test_name << std::endl;
            return true;  // Return true since this is expected behavior in CI
        }

        std::cout << "🔍 IMAGE DIFF TRACE: Launching image comparison for " << diff_info.test_name
                  << std::endl;
        std::cout << "🔍 IMAGE DIFF TRACE: Approved: " << diff_info.approved_path << std::endl;
        std::cout << "🔍 IMAGE DIFF TRACE: Received: " << diff_info.received_path << std::endl;

        // Use VS Code's diff functionality for side-by-side image comparison
        std::string command =
            "code --diff \"" + diff_info.approved_path + "\" \"" + diff_info.received_path + "\"";
        std::cout << "🔍 IMAGE DIFF TRACE: Executing image diff command: " << command << std::endl;

        bool success = ExecuteCommand(command);

        if (success) {
            std::cout << "To approve image changes, run:" << std::endl;
            std::cout << "  copy \"" << diff_info.received_path << "\" \""
                      << diff_info.approved_path << "\"" << std::endl;
        }

        return success;
    }

    bool VSCodeDiffLauncher::ShouldLaunchDiff() const {
        return !config_.IsRunningInCI();
    }

    bool VSCodeDiffLauncher::ExecuteCommand(const std::string& command) const {
        try {
            int result = std::system(command.c_str());
            return result == 0;
        } catch (const std::exception&) {
            return false;
        }
    }

    bool VSCodeDiffLauncher::CreateEmptyBaseline(const std::string& baseline_path,
                                                 const std::string& content_type) const {
        try {
            std::ofstream empty_file(baseline_path);
            if (content_type == "text") {
                empty_file << "# This is a new test - no baseline exists yet\n";
                empty_file << "# Review the received content and approve if correct\n";
            }
            empty_file.close();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    // CIDiffLauncher Implementation
    bool CIDiffLauncher::LaunchTextDiff(const DiffInfo& diff_info) const {
        std::cout << "CI Mode: Text diff requested for " << diff_info.test_name
                  << " but interactive diff disabled in CI environment" << std::endl;
        std::cout << "Approved: " << diff_info.approved_path << std::endl;
        std::cout << "Received: " << diff_info.received_path << std::endl;
        return true;
    }

    bool CIDiffLauncher::LaunchImageDiff(const DiffInfo& diff_info) const {
        std::cout << "CI Mode: Image diff requested for " << diff_info.test_name
                  << " but interactive diff disabled in CI environment" << std::endl;
        std::cout << "Approved: " << diff_info.approved_path << std::endl;
        std::cout << "Received: " << diff_info.received_path << std::endl;
        return true;
    }

    bool CIDiffLauncher::ShouldLaunchDiff() const {
        return false;  // Never launch in CI mode
    }

    // DiffLauncherFactory Implementation
    std::unique_ptr<IDiffLauncher> DiffLauncherFactory::Create(LauncherType type,
                                                               const ApprovalTestConfig& config) {
        switch (type) {
            case LauncherType::AUTO_DETECT:
                if (config.IsRunningInCI()) {
                    return std::make_unique<CIDiffLauncher>();
                } else {
                    return std::make_unique<VSCodeDiffLauncher>(config);
                }
            case LauncherType::VSCODE:
                return std::make_unique<VSCodeDiffLauncher>(config);
            case LauncherType::CI_NOOP:
                return std::make_unique<CIDiffLauncher>();
            default:
                return std::make_unique<VSCodeDiffLauncher>(config);
        }
    }

}  // namespace golf_sim::image_analysis::testing
