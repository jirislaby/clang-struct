module ApplicationHelper
  def src_link_to(text, file, line = 0)
    @rev = "v6.7"
    @link = "https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/#{file}?id=#{@rev}"
    if line > 0
      @link << "#n#{line}"
    end
    link_to(text, @link)
  end
end
