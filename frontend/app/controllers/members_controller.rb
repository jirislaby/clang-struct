class MembersController < ApplicationController
  def index
    @members = Member
    if params[:unused] == '1'
      @members = @members.unused
    end
    if params[:filter] != ''
      @filter = "%#{params[:filter]}%"
      @members = @members.where('member.name LIKE ? OR struct.name LIKE ?',
                                @filter, @filter)
    end
    if params[:filter_file] != ''
      @filter = "%#{params[:filter_file]}%"
      @members = @members.where('source.src LIKE ?', @filter)
    end
    @members = @members.joins({:struct => :source}).order('struct.name, member.begLine').limit(500);

    respond_to do |format|
      format.html
    end
  end

end
