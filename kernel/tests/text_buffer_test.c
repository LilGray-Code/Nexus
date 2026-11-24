#include "text_buffer.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void test_insert_middle_and_capacity(void) {
    char data[16] = "abcd";
    size_t length = 4U, cursor = 2U;
    assert(nexus_text_buffer_insert(data,sizeof(data),&length,&cursor,'X'));
    assert(length==5U && cursor==3U && strcmp(data,"abXcd")==0);
    cursor=length;
    assert(nexus_text_buffer_insert(data,sizeof(data),&length,&cursor,'!'));
    assert(strcmp(data,"abXcd!")==0 && cursor==6U);
    char full[4] = "abc";
    length=3U; cursor=1U;
    assert(!nexus_text_buffer_insert(full,sizeof(full),&length,&cursor,'x'));
    assert(strcmp(full,"abc")==0 && length==3U && cursor==1U);
    assert(!nexus_text_buffer_insert(data,sizeof(data),&length,&cursor,'\0'));
}

static void test_backspace_and_delete(void) {
    char data[16] = "abcdef";
    size_t length=6U, cursor=3U;
    assert(nexus_text_buffer_backspace(data,sizeof(data),&length,&cursor));
    assert(strcmp(data,"abdef")==0 && length==5U && cursor==2U);
    assert(nexus_text_buffer_delete(data,sizeof(data),&length,cursor));
    assert(strcmp(data,"adef")==0 && length==4U);
    assert(!nexus_text_buffer_backspace(data,sizeof(data),&length,&(size_t){0U}));
    assert(!nexus_text_buffer_delete(data,sizeof(data),&length,length));
}

static void test_horizontal_and_line_navigation(void) {
    char data[32] = "first\nsecond\nthird";
    size_t length=strlen(data), cursor=0U;
    nexus_text_buffer_move_right(length,&cursor);
    nexus_text_buffer_move_right(length,&cursor);
    nexus_text_buffer_move_end(data,length,&cursor);
    assert(cursor==5U); /* Before the first newline. */
    nexus_text_buffer_move_down(data,length,&cursor);
    assert(cursor==11U); /* Same column in "second". */
    nexus_text_buffer_move_home(data,length,&cursor);
    assert(cursor==6U);
    nexus_text_buffer_move_up(data,length,&cursor);
    assert(cursor==0U);
    nexus_text_buffer_move_left(length,&cursor);
    assert(cursor==0U);
    nexus_text_buffer_move_end(data,length,&cursor);
    assert(cursor==5U);
}

static void test_vertical_clamps_and_empty_lines(void) {
    char data[32] = "longline\nx\n\nlast";
    size_t length=strlen(data), cursor=7U;
    nexus_text_buffer_move_down(data,length,&cursor);
    assert(cursor==10U); /* Clamp to the one-character line end. */
    nexus_text_buffer_move_down(data,length,&cursor);
    assert(cursor==11U); /* Empty line. */
    nexus_text_buffer_move_down(data,length,&cursor);
    assert(cursor==12U); /* Clamped to the start of "last". */
    nexus_text_buffer_move_down(data,length,&cursor);
    assert(cursor==12U);
    nexus_text_buffer_move_up(data,length,&cursor);
    assert(cursor==11U);
}

int main(void) {
    test_insert_middle_and_capacity();
    test_backspace_and_delete();
    test_horizontal_and_line_navigation();
    test_vertical_clamps_and_empty_lines();
    return 0;
}
