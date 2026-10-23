#include <unity.h>
#include "pairingState.h"
#include <limits>

using namespace Pairing;
void setUp() {}
void tearDown() {}

void challenge_and_response()
{
    State state(42);
    auto id = state.create({42, 1}, Type::PasskeyEntry, 0, 100, 25000);
    TEST_ASSERT_TRUE(id.has_value());
    TEST_ASSERT_EQUAL_UINT64(25100, *state.nextDeadline());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::InvalidArgument),
        static_cast<int>(state.respond(*id, Type::PasskeyEntry, 1000000, 101)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::Claimed),
        static_cast<int>(state.respond(*id, Type::PasskeyEntry, 0, 101)));
    // Zero is a valid operator response, never a timeout sentinel.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::NotPending),
        static_cast<int>(state.respond(*id, Type::PasskeyEntry, 123456, 102)));
    TEST_ASSERT_FALSE(state.nextDeadline().has_value());
}
void expiration_and_deadline_boundary()
{
    State state(42);
    auto id = *state.create({42, 1}, Type::NumericConfirmation, 123456, 100, 25000);
    TEST_ASSERT_FALSE(state.expireOne(25099).has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::Expired),
        static_cast<int>(state.respond(id, Type::NumericConfirmation, 1, 25100)));
    auto expired = state.expireOne(25100);
    TEST_ASSERT_TRUE(expired.has_value());
    TEST_ASSERT_TRUE(expired->connection == (ConnectionId{42, 1}));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::Expired),
        static_cast<int>(state.respond(id, Type::NumericConfirmation, 1, 25101)));
    TEST_ASSERT_FALSE(state.expireOne(25100).has_value());
}
void independent_deadlines_and_next_expiration()
{
    State state(42);
    auto later = *state.create({42, 1}, Type::PasskeyEntry, 0, 100, 100);
    auto sooner = *state.create({42, 2}, Type::NumericConfirmation, 123456, 110, 20);
    TEST_ASSERT_EQUAL_UINT64(130, *state.nextDeadline());
    TEST_ASSERT_FALSE(state.expireOne(129).has_value());

    auto expired = state.expireOne(130);
    TEST_ASSERT_TRUE(expired.has_value());
    TEST_ASSERT_TRUE(expired->id == sooner);
    TEST_ASSERT_EQUAL_UINT64(200, *state.nextDeadline());
    TEST_ASSERT_FALSE(state.expireOne(199).has_value());

    expired = state.expireOne(200);
    TEST_ASSERT_TRUE(expired.has_value());
    TEST_ASSERT_TRUE(expired->id == later);
    TEST_ASSERT_FALSE(state.nextDeadline().has_value());
}
void lifecycle_and_stale_ids()
{
    State state(42);
    auto old = *state.create({42, 1}, Type::PasskeyEntry, 0, 0, 25);
    state.invalidate({42, 1}, false);
    auto fresh = *state.create({42, 2}, Type::PasskeyEntry, 0, 1, 25);
    TEST_ASSERT_TRUE(old != fresh);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::NotFound),
        static_cast<int>(state.respond(old, Type::PasskeyEntry, 123456, 2)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::NotFound),
        static_cast<int>(state.respond({41, fresh.sequence}, Type::PasskeyEntry, 123456, 2)));
    state.invalidate({42, 1}, true);
    TEST_ASSERT_TRUE(state.nextDeadline().has_value());
    state.invalidate({42, 2}, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::NotFound),
        static_cast<int>(state.respond(fresh, Type::PasskeyEntry, 123456, 2)));
    TEST_ASSERT_FALSE(state.nextDeadline().has_value());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Resolution::Completed), static_cast<int>(state.requests()[0].resolution));
    TEST_ASSERT_FALSE(state.create({41, 1}, Type::PasskeyEntry, 0, 0, 25).has_value());
}
void capacity_and_device_display()
{
    State state(42);
    for (uint32_t i = 1; i <= State::Capacity; ++i)
        TEST_ASSERT_TRUE(state.create({42, i}, Type::PasskeyEntry, 0, 0, 25).has_value());
    TEST_ASSERT_FALSE(state.create({42, 1}, Type::NumericConfirmation, 123456, 1, 25).has_value());
    TEST_ASSERT_FALSE(state.create({42, 9}, Type::PasskeyEntry, 0, 0, 25).has_value());
    state.invalidate({42, 3}, false);
    TEST_ASSERT_TRUE(state.create({42, 9}, Type::PasskeyEntry, 0, 0, 25).has_value());
    TEST_ASSERT_FALSE(state.create({42, 9}, Type::NumericConfirmation, 123456, 0, 25).has_value());
    State display(42);
    auto id = *display.create({}, Type::Display, 123456, 0, 25);
    TEST_ASSERT_FALSE(static_cast<bool>(display.requests()[0].connection));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::InvalidArgument),
        static_cast<int>(display.respond(id, Type::Display, 0, 1)));
    display.invalidate({42, 5}, false);
    TEST_ASSERT_TRUE(display.expireOne(25).has_value());
}
void independent_device_display_notifications_and_capacity()
{
    State state(42);
    auto firstDisplay = state.create({}, Type::Display, 123456, 100, 100);
    auto secondDisplay = state.create({}, Type::Display, 654321, 110, 20);
    TEST_ASSERT_TRUE(firstDisplay.has_value());
    TEST_ASSERT_TRUE(secondDisplay.has_value());
    TEST_ASSERT_TRUE(*firstDisplay != *secondDisplay);

    for (uint32_t i = 1; i < State::Capacity - 1; ++i)
        TEST_ASSERT_TRUE(state.create({42, i}, Type::PasskeyEntry, 0, 100, 300).has_value());
    TEST_ASSERT_FALSE(state.create({}, Type::Display, 222222, 120, 25).has_value());
    TEST_ASSERT_EQUAL_UINT64(130, *state.nextDeadline());
    TEST_ASSERT_FALSE(static_cast<bool>(state.requests()[0].connection));
    TEST_ASSERT_EQUAL_UINT32(123456, state.requests()[0].value);
    TEST_ASSERT_FALSE(static_cast<bool>(state.requests()[1].connection));
    TEST_ASSERT_EQUAL_UINT32(654321, state.requests()[1].value);

    auto expired = state.expireOne(130);
    TEST_ASSERT_TRUE(expired.has_value());
    TEST_ASSERT_TRUE(expired->id == *secondDisplay);
    TEST_ASSERT_EQUAL_UINT64(200, *state.nextDeadline());

    auto replacement = state.create({}, Type::Display, 222222, 131, 25);
    TEST_ASSERT_TRUE(replacement.has_value());
    TEST_ASSERT_TRUE(*replacement != *secondDisplay);
    TEST_ASSERT_FALSE(static_cast<bool>(state.requests()[1].connection));
    TEST_ASSERT_EQUAL_UINT32(222222, state.requests()[1].value);
}
void creation_validation_and_change_counter()
{
    State disabled(0);
    TEST_ASSERT_FALSE(disabled.create({0, 1}, Type::PasskeyEntry, 0, 0, 1).has_value());

    State state(42);
    TEST_ASSERT_FALSE(state.create({}, Type::PasskeyEntry, 0, 0, 1).has_value());
    TEST_ASSERT_FALSE(state.create({41, 1}, Type::PasskeyEntry, 0, 0, 1).has_value());
    TEST_ASSERT_FALSE(state.create({42, 1}, Type::PasskeyEntry, 0, 0, 0).has_value());
    TEST_ASSERT_FALSE(state.create({42, 1}, Type::PasskeyEntry, 0,
        std::numeric_limits<uint64_t>::max(), 1).has_value());
    TEST_ASSERT_FALSE(state.create({42, 1}, Type::PasskeyEntry, 1000000, 0, 1).has_value());
    TEST_ASSERT_EQUAL_UINT64(0, state.changes());

    auto id = state.create({42, 1}, Type::PasskeyEntry, 999999, 100, 25);
    TEST_ASSERT_TRUE(id.has_value());
    TEST_ASSERT_EQUAL_UINT64(1, state.changes());
    TEST_ASSERT_FALSE(state.create({42, 1}, Type::NumericConfirmation, 123456, 101, 25).has_value());
    TEST_ASSERT_EQUAL_UINT64(1, state.changes());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::InvalidArgument),
        static_cast<int>(state.respond(*id, Type::PasskeyEntry, 1000000, 101)));
    TEST_ASSERT_EQUAL_UINT64(1, state.changes());

    state.invalidate({42, 1}, false);
    TEST_ASSERT_EQUAL_UINT64(2, state.changes());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Resolution::Cancelled),
        static_cast<int>(state.requests()[0].resolution));
}
void first_response_and_type_validation()
{
    State state(42);
    auto id = *state.create({42, 1}, Type::NumericConfirmation, 987654, 0, 25);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::InvalidArgument),
        static_cast<int>(state.respond(id, Type::PasskeyEntry, 123456, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::InvalidArgument),
        static_cast<int>(state.respond(id, Type::NumericConfirmation, 2, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::Claimed),
        static_cast<int>(state.respond(id, Type::NumericConfirmation, 0, 1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(ResponseResult::NotPending),
        static_cast<int>(state.respond(id, Type::NumericConfirmation, 1, 1)));
    TEST_ASSERT_FALSE(state.expireOne(100).has_value());
    state.invalidate({42, 1}, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Resolution::Completed), static_cast<int>(state.requests()[0].resolution));
}
int main()
{
    UNITY_BEGIN();
    RUN_TEST(challenge_and_response);
    RUN_TEST(expiration_and_deadline_boundary);
    RUN_TEST(independent_deadlines_and_next_expiration);
    RUN_TEST(lifecycle_and_stale_ids);
    RUN_TEST(capacity_and_device_display);
    RUN_TEST(independent_device_display_notifications_and_capacity);
    RUN_TEST(creation_validation_and_change_counter);
    RUN_TEST(first_response_and_type_validation);
    return UNITY_END();
}
