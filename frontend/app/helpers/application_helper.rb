module ApplicationHelper
  def src_link_to(text, ver, file, line = 0)
    @link = "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/#{file}?id=#{ver}"
    if line > 0
      @link << "#n#{line}"
    end
    link_to(text, @link)
  end

  def generate_order_button(symbol, name, order_dir)
    link_to(symbol, request.params.merge({order: name, order_dir: order_dir, page: nil}))
  end

  def generate_order_buttons(name)
    ret = name + ' ' <<
      generate_order_button('▲', name, 'asc') <<
      generate_order_button('▼', name, 'desc')
    ret.html_safe
  end
end
