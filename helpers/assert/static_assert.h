/**
 * @file static_assert.h
 * @brief Compile-time assertions
 */
#ifndef __STATIC_ASSERT_H__
#define __STATIC_ASSERT_H__

#define STATIC_ASSERT(expr)  _Static_assert(expr, #expr)

#endif /* __STATIC_ASSERT_H__ */
