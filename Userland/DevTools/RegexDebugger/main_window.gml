@GUI::Frame {
    layout: @GUI::VerticalBoxLayout {
    }

    fixed_height: 480
    fixed_width: 640

    @GUI::VerticalSplitter {
        @GUI::Widget {
            shrink_to_fit: true

            layout: @GUI::HorizontalBoxLayout {
                margins: [4, 4]
            }

            @GUI::Label {
                text: "Regex:"
                autosize: true
            }

            @GUI::TextBox {
                name: "regex_input"
                font_type: "FixedWidth"
            }
        }

        @GUI::HorizontalSplitter {
            layout: @GUI::HorizontalBoxLayout {
                margins: [4, 4]
            }

            @GUI::TextEditor {
                name: "text_input"
            }

            @GUI::VerticalSplitter {
                @GUI::Widget {
                    fixed_height: 80

                    layout: @GUI::VerticalBoxLayout {
                    }

                    @GUI::Widget {
                        layout: @GUI::HorizontalBoxLayout {
                        }

                        @GUI::SpinBox {
                            name: "step_spinbox"
                            fixed_width: 40
                        }

                        @GUI::Slider {
                            name: "step_slider"
                            orientation: "Horizontal"
                            max: 100
                        }
                    }

                    @GUI::Widget {
                        layout: @GUI::HorizontalBoxLayout {
                            margins: [4]
                            spacing: 4
                        }

                        @GUI::Button {
                            text: "First"
                            name: "jump_to_first_button"
                        }

                        @GUI::Button {
                            text: "Previous"
                            name: "jump_to_previous_button"
                        }

                        @GUI::Button {
                            text: "Next"
                            name: "jump_to_next_button"
                        }

                        @GUI::Button {
                            text: "Last"
                            name: "jump_to_last_button"
                        }
                    }
                }

                @GUI::TextEditor {
                    name: "bytecode_viewer"
                    mode: "ReadOnly"
                    // text: "Compare Char('x')\nCompare Inverse, Reference('foo')"
                }
            }
        }
    }
}
