/*
 * Camera Domain Tests
 *
 * Unit tests for the camera domain interface.
 * Tests the domain types and their behavior without platform dependencies.
 */

#include <boost/test/unit_test.hpp>
#include "domain/camera_domain.hpp"

BOOST_AUTO_TEST_SUITE(DomainTests)

BOOST_AUTO_TEST_CASE(size_default_construction) {
    // Arrange
    // (No setup needed for default construction)

    // Act
    golf_sim::camera::domain::Size size;

    // Assert
    BOOST_CHECK_EQUAL(size.width, 0);
    BOOST_CHECK_EQUAL(size.height, 0);
}

BOOST_AUTO_TEST_CASE(size_parameterized_construction) {
    // Arrange
    const unsigned int expected_width = 1920;
    const unsigned int expected_height = 1080;

    // Act
    golf_sim::camera::domain::Size size(expected_width, expected_height);

    // Assert
    BOOST_CHECK_EQUAL(size.width, expected_width);
    BOOST_CHECK_EQUAL(size.height, expected_height);
}

BOOST_AUTO_TEST_CASE(size_copy_construction) {
    // Arrange
    golf_sim::camera::domain::Size original(800, 600);

    // Act
    golf_sim::camera::domain::Size copy(original);

    // Assert
    BOOST_CHECK_EQUAL(copy.width, original.width);
    BOOST_CHECK_EQUAL(copy.height, original.height);
}

BOOST_AUTO_TEST_CASE(transform_default_construction) {
    // Arrange
    // (No setup needed for default construction)

    // Act
    golf_sim::camera::domain::Transform transform;

    // Assert
    BOOST_CHECK_EQUAL(transform.value, 0);
}

BOOST_AUTO_TEST_CASE(transform_parameterized_construction) {
    // Arrange
    const int expected_value = 90;  // Typical rotation value

    // Act
    golf_sim::camera::domain::Transform transform(expected_value);

    // Assert
    BOOST_CHECK_EQUAL(transform.value, expected_value);
}

BOOST_AUTO_TEST_CASE(pixel_format_default_construction) {
    // Arrange
    // (No setup needed for default construction)

    // Act
    golf_sim::camera::domain::PixelFormat format;

    // Assert
    BOOST_CHECK_EQUAL(format.fourcc, 0);
}

BOOST_AUTO_TEST_CASE(pixel_format_parameterized_construction) {
    // Arrange
    const unsigned int expected_fourcc = 0x32315659;  // YV12 format

    // Act
    golf_sim::camera::domain::PixelFormat format(expected_fourcc);

    // Assert
    BOOST_CHECK_EQUAL(format.fourcc, expected_fourcc);
}

BOOST_AUTO_TEST_CASE(color_space_default_construction) {
    // Arrange
    // (No setup needed for default construction)

    // Act
    golf_sim::camera::domain::ColorSpace colorSpace;

    // Assert
    BOOST_CHECK_EQUAL(colorSpace.value, 0);
}

BOOST_AUTO_TEST_CASE(color_space_parameterized_construction) {
    // Arrange
    const int expected_value = 1;  // Typical color space identifier

    // Act
    golf_sim::camera::domain::ColorSpace colorSpace(expected_value);

    // Assert
    BOOST_CHECK_EQUAL(colorSpace.value, expected_value);
}

BOOST_AUTO_TEST_CASE(domain_types_are_value_types) {
    // Arrange
    golf_sim::camera::domain::Size size1(1920, 1080);
    golf_sim::camera::domain::Size size2(1920, 1080);
    golf_sim::camera::domain::Transform transform1(90);
    golf_sim::camera::domain::Transform transform2(90);

    // Act & Assert - Test that objects with same values behave as value types
    BOOST_CHECK_EQUAL(size1.width, size2.width);
    BOOST_CHECK_EQUAL(size1.height, size2.height);
    BOOST_CHECK_EQUAL(transform1.value, transform2.value);
}

BOOST_AUTO_TEST_CASE(size_supports_zero_dimensions) {
    // Arrange
    // (Testing edge case of zero dimensions)

    // Act
    golf_sim::camera::domain::Size zero_size(0, 0);
    golf_sim::camera::domain::Size zero_width(0, 480);
    golf_sim::camera::domain::Size zero_height(640, 0);

    // Assert
    BOOST_CHECK_EQUAL(zero_size.width, 0);
    BOOST_CHECK_EQUAL(zero_size.height, 0);
    BOOST_CHECK_EQUAL(zero_width.width, 0);
    BOOST_CHECK_EQUAL(zero_width.height, 480);
    BOOST_CHECK_EQUAL(zero_height.width, 640);
    BOOST_CHECK_EQUAL(zero_height.height, 0);
}

BOOST_AUTO_TEST_CASE(types_support_large_values) {
    // Arrange
    const unsigned int max_dimension = 4294967295U;  // Close to UINT_MAX
    const int max_transform = 2147483647;            // Close to INT_MAX
    const unsigned int max_fourcc = 4294967295U;     // Close to UINT_MAX

    // Act
    golf_sim::camera::domain::Size large_size(max_dimension, max_dimension);
    golf_sim::camera::domain::Transform large_transform(max_transform);
    golf_sim::camera::domain::PixelFormat large_format(max_fourcc);
    golf_sim::camera::domain::ColorSpace large_colorspace(max_transform);

    // Assert
    BOOST_CHECK_EQUAL(large_size.width, max_dimension);
    BOOST_CHECK_EQUAL(large_size.height, max_dimension);
    BOOST_CHECK_EQUAL(large_transform.value, max_transform);
    BOOST_CHECK_EQUAL(large_format.fourcc, max_fourcc);
    BOOST_CHECK_EQUAL(large_colorspace.value, max_transform);
}

BOOST_AUTO_TEST_SUITE_END()
